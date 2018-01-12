// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "StaticMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "MeshDescriptionHelper.h"
#include "BuildOptimizationHelper.h"
#include "Components.h"
#include "IMeshReductionManagerModule.h"
#include "MeshBuild.h"

DEFINE_LOG_CATEGORY(LogStaticMeshBuilder);

//////////////////////////////////////////////////////////////////////////
//Local functions definition
bool IsOrphanedVertex(const class UMeshDescription *MeshDescription, const FVertexID VertexID);
void UpdateBounds(class UStaticMesh* StaticMesh);
void UpdateCollision(UStaticMesh *StaticMesh);
void BuildVertexBuffer(
	  UStaticMesh *StaticMesh
	, int32 LodIndex
	, UMeshDescription* MeshDescription
	, FStaticMeshLODResources& StaticMeshLOD
	, const FMeshBuildSettings& LODBuildSettings
	, TArray< uint32 >& IndexBuffer
	, TArray<int32>& OutWedgeMap
	, TArray<TArray<uint32> >& OutPerSectionIndices
	, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices
	, const TMultiMap<int32, int32>& OverlappingCorners
	, float VertexComparisonThreshold
	, TArray<int32>& RemapVerts);
void BuildAllBufferOptimizations(struct FStaticMeshLODResources& StaticMeshLOD, const struct FMeshBuildSettings& LODBuildSettings, TArray< uint32 >& IndexBuffer, bool bNeeds32BitIndices, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices);
//////////////////////////////////////////////////////////////////////////

FStaticMeshBuilder::FStaticMeshBuilder()
{

}

bool FStaticMeshBuilder::Build(UStaticMesh* StaticMesh, const FStaticMeshLODGroup& LODGroup)
{
	if (StaticMesh->GetOriginalMeshDescription(0) == nullptr)
	{
		//Warn the user that there is no mesh description data
		UE_LOG(LogStaticMeshBuilder, Error, TEXT("Cannot find a valid mesh description to build the asset."));
		return false;
	}
	StaticMesh->RenderData->AllocateLODResources(StaticMesh->SourceModels.Num());

	//OnBuildRenderMeshStart(StaticMesh, false);

	FStaticMeshRenderData& StaticMeshRenderData = *StaticMesh->RenderData;
	for (int32 LodIndex = 0; LodIndex < StaticMesh->SourceModels.Num(); ++LodIndex)
	{
		FMeshBuildSettings& LODBuildSettings = StaticMesh->SourceModels[LodIndex].BuildSettings;
		UMeshDescription* OriginalMeshDescription = StaticMesh->GetOriginalMeshDescription(LodIndex);
		FMeshDescriptionHelper MeshDescriptionHelper(&LODBuildSettings, OriginalMeshDescription);
		
		const FStaticMeshSourceModel& SrcModel = StaticMesh->SourceModels[LodIndex];
		FMeshReductionSettings ReductionSettings = LODGroup.GetSettings(SrcModel.ReductionSettings, LodIndex);

		UMeshDescription* MeshDescription = nullptr;
		bool bUseReduction = (ReductionSettings.PercentTriangles < 1.0f || ReductionSettings.MaxDeviation > 0.0f) && (StaticMesh->GetMeshDescription(0) != nullptr);

		if (OriginalMeshDescription != nullptr)
		{
			OriginalMeshDescription->TriangulateMesh(); //Make sure the original mesh description is triangulate.
			MeshDescription = MeshDescriptionHelper.GetRenderMeshDescription(StaticMesh);
		}
		
		if(MeshDescription == nullptr)
		{
			if (bUseReduction)
			{
				//Create an empty mesh description that the reduce will fill
				MeshDescription = NewObject<UMeshDescription>(StaticMesh, NAME_None);
			}
			else if(StaticMesh->GetMeshDescription(0) != nullptr)
			{
				//Duplicate the lodindex 0 we have a 100% reduction which is like a duplicate
				MeshDescription = Cast<UMeshDescription>(StaticDuplicateObject(StaticMesh->GetMeshDescription(0), StaticMesh, NAME_None, RF_NoFlags));
			}
		}

		check(MeshDescription != nullptr);
		StaticMesh->SetMeshDescription(LodIndex, MeshDescription);
		
		//Reduce LODs
		if (bUseReduction)
		{
			float OverlappingThreshold = LODBuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;
			TMultiMap<int32, int32> OverlappingCorners;
			MeshDescriptionHelper.FindOverlappingCorners(OverlappingCorners, StaticMesh->GetMeshDescription(0), OverlappingThreshold);
			MeshDescriptionHelper.ReduceLOD(StaticMesh->GetMeshDescription(0), StaticMesh->GetMeshDescription(LodIndex), ReductionSettings, OverlappingCorners);
			// Recompute adjacency information. Since we change the vertices when we reduce
			MeshDescriptionHelper.FindOverlappingCorners(StaticMesh->GetMeshDescription(LodIndex), OverlappingThreshold);
		}

		const FPolygonGroupArray& PolygonGroups = MeshDescription->PolygonGroups();

		FStaticMeshLODResources& StaticMeshLOD = StaticMeshRenderData.LODResources[LodIndex];

		//discover degenerate triangle with this threshold
		float VertexComparisonThreshold = LODBuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;

		//Build new vertex buffers
		TArray< FStaticMeshBuildVertex > StaticMeshBuildVertices;

		TArray< uint32 > IndexBuffer;

		StaticMeshLOD.Sections.Empty(PolygonGroups.Num());
		TArray<int32> RemapVerts; //Because we will remove MeshVertex that are redundant, we need a remap
								  //Render data Wedge map is only set for LOD 0???
		TArray<int32> TempWedgeMap;
		TArray<int32> &WedgeMap = (LodIndex == 0) ? StaticMeshRenderData.WedgeMap : TempWedgeMap;

		//Prepare the PerSectionIndices array so we can optimize the index buffer for the GPU
		int32 MaxMaterialIndex = 1;
		for (const FPolygonGroupID& PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
		{
			MaxMaterialIndex = FMath::Max<int32>(PolygonGroupID.GetValue(), MaxMaterialIndex);
		}
		TArray<TArray<uint32> > PerSectionIndices;
		for (int32 i = 0; i <= MaxMaterialIndex; ++i)
		{
			PerSectionIndices.Push(TArray<uint32>());
		}

		//Build the vertex and index buffer
		BuildVertexBuffer(StaticMesh, LodIndex, MeshDescription, StaticMeshLOD, LODBuildSettings, IndexBuffer, WedgeMap, PerSectionIndices, StaticMeshBuildVertices, MeshDescriptionHelper.GetOverlappingCorners(), VertexComparisonThreshold, RemapVerts);

		// Figure out which index buffer stride we need
		bool bNeeds32BitIndices = false;
		for (const FStaticMeshSection& StaticMeshSection : StaticMeshLOD.Sections)
		{
			if (StaticMeshSection.MaxVertexIndex > TNumericLimits<uint16>::Max())
			{
				bNeeds32BitIndices = true;
				break;
			}
		}
		const EIndexBufferStride::Type IndexBufferStride = bNeeds32BitIndices ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit;
		StaticMeshLOD.IndexBuffer.SetIndices(IndexBuffer, IndexBufferStride);

		BuildAllBufferOptimizations(StaticMeshLOD, LODBuildSettings, IndexBuffer, bNeeds32BitIndices, StaticMeshBuildVertices);
	} //End of LOD for loop

	// Calculate the bounding box.
	FBox BoundingBox(ForceInit);
	FPositionVertexBuffer& BasePositionVertexBuffer = StaticMeshRenderData.LODResources[0].PositionVertexBuffer;
	for (uint32 VertexIndex = 0; VertexIndex < BasePositionVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		BoundingBox += BasePositionVertexBuffer.VertexPosition(VertexIndex);
	}
	BoundingBox.GetCenterAndExtents(StaticMeshRenderData.Bounds.Origin, StaticMeshRenderData.Bounds.BoxExtent);

	// Calculate the bounding sphere, using the center of the bounding box as the origin.
	StaticMeshRenderData.Bounds.SphereRadius = 0.0f;
	for (uint32 VertexIndex = 0; VertexIndex < BasePositionVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		StaticMeshRenderData.Bounds.SphereRadius = FMath::Max(
			(BasePositionVertexBuffer.VertexPosition(VertexIndex) - StaticMeshRenderData.Bounds.Origin).Size(),
			StaticMeshRenderData.Bounds.SphereRadius
		);
	}
	//OnBuildRenderMeshFinish(StaticMesh, true);

	return true;
}

void FStaticMeshBuilder::OnBuildRenderMeshStart(UStaticMesh* StaticMesh, const bool bInvalidateLighting)
{
	// We may already have a lock on the rendering resources, if it wasn't released the last time we called EndModification()
	// on the mesh.  This is only the case when rolling back preview changes for a mesh, because we're guaranteed to apply another
	// modification to the same mesh in the very same frame.  So we can avoid having to update the GPU resources twice in one frame.
	if (!RecreateRenderStateContext.IsValid())
	{
		// We're changing the mesh itself, so ALL static mesh components in the scene will need
		// to be unregistered for this (and reregistered afterwards.)
		const bool bRefreshBounds = true;
		RecreateRenderStateContext = MakeShareable(new FStaticMeshComponentRecreateRenderStateContext(StaticMesh, bInvalidateLighting, bRefreshBounds));

		// Release the static mesh's resources.
		StaticMesh->ReleaseResources();

		// Flush the resource release commands to the rendering thread to ensure that the build doesn't occur while a resource is still
		// allocated, and potentially accessing the UStaticMesh.
		StaticMesh->ReleaseResourcesFence.Wait();
	}
}

void FStaticMeshBuilder::OnBuildRenderMeshFinish(UStaticMesh* StaticMesh, const bool bRebuildBoundsAndCollision)
{
	if (bRebuildBoundsAndCollision)
	{
		UpdateBounds(StaticMesh);
		UpdateCollision(StaticMesh);
	}

	StaticMesh->InitResources();

	// NOTE: This can call InvalidateLightingCache() on all components using this mesh, causing Modify() to be 
	// called on those components!  Just something to be aware of when EndModification() is called within
	// an undo transaction.
	RecreateRenderStateContext.Reset();
}

bool IsOrphanedVertex(const UMeshDescription *MeshDescription, const FVertexID VertexID)
{
	for (const FVertexInstanceID VertexInstanceID : MeshDescription->GetVertexVertexInstances(VertexID))
	{
		if (MeshDescription->GetVertexInstanceConnectedPolygons(VertexInstanceID).Num() > 0)
		{
			return false;
		}
	}

	return true;
}

void UpdateBounds(UStaticMesh* StaticMesh)
{
	const UMeshDescription* MeshDescription = StaticMesh->GetMeshDescription();
	check(MeshDescription != nullptr);

	// Compute a new bounding box
	// @todo mesheditor perf: During the final modification, only do this if the bounds may have changed (need hinting)
	FBoxSphereBounds BoundingBoxAndSphere;

	FBox BoundingBox;
	BoundingBox.Init();

	// Could improve performance here if necessary:
	// 1) cache polygon IDs per vertex (in order to quickly reject orphans) and just iterate vertex array; or
	// 2) cache bounding box per polygon
	// There are other cases where having polygon adjacency information (1) might be useful, so it's maybe worth considering.

	const FVertexArray& Vertices = MeshDescription->Vertices();
	const FVertexInstanceArray& VertexInstances = MeshDescription->VertexInstances();

	const TVertexAttributeArray<FVector>& VertexPositions = MeshDescription->VertexAttributes().GetAttributes<FVector>(MeshAttribute::Vertex::Position);

	for (const FVertexID VertexID : Vertices.GetElementIDs())
	{
		if (!IsOrphanedVertex(MeshDescription, VertexID))
		{
			BoundingBox += VertexPositions[VertexID];
		}
	}

	BoundingBox.GetCenterAndExtents( /* Out */ BoundingBoxAndSphere.Origin, /* Out */ BoundingBoxAndSphere.BoxExtent);

	// Calculate the bounding sphere, using the center of the bounding box as the origin.
	BoundingBoxAndSphere.SphereRadius = 0.0f;

	for (const FVertexID VertexID : Vertices.GetElementIDs())
	{
		if (!IsOrphanedVertex(MeshDescription, VertexID))
		{
			BoundingBoxAndSphere.SphereRadius = FMath::Max((VertexPositions[VertexID] - BoundingBoxAndSphere.Origin).Size(), BoundingBoxAndSphere.SphereRadius);
		}
	}

	FStaticMeshRenderData& StaticMeshRenderData = *StaticMesh->RenderData;
	StaticMeshRenderData.Bounds = BoundingBoxAndSphere;
	StaticMesh->CalculateExtendedBounds();
}


void UpdateCollision(UStaticMesh *StaticMesh)
{
	// @todo mesheditor collision: We're wiping the existing simplified collision and generating a simple bounding
	// box collision, since that's the best we can do without impacting performance.  We always using visibility (complex)
	// collision for traces while mesh editing (for hover/selection), so simplified collision isn't really important.
	const bool bRecreateSimplifiedCollision = true;

	if (StaticMesh->BodySetup == nullptr)
	{
		StaticMesh->CreateBodySetup();
	}

	UBodySetup* BodySetup = StaticMesh->BodySetup;

	// NOTE: We don't bother calling Modify() on the BodySetup as EndModification() will rebuild this guy after every undo
	// BodySetup->Modify();

	if (bRecreateSimplifiedCollision)
	{
		if (BodySetup->AggGeom.GetElementCount() > 0)
		{
			BodySetup->RemoveSimpleCollision();
		}
	}

	BodySetup->InvalidatePhysicsData();

	if (bRecreateSimplifiedCollision)
	{
		const FBoxSphereBounds Bounds = StaticMesh->GetBounds();

		FKBoxElem BoxElem;
		BoxElem.Center = Bounds.Origin;
		BoxElem.X = Bounds.BoxExtent.X * 2.0f;
		BoxElem.Y = Bounds.BoxExtent.Y * 2.0f;
		BoxElem.Z = Bounds.BoxExtent.Z * 2.0f;
		BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}

	// Update all static mesh components that are using this mesh
	// @todo mesheditor perf: This is a pretty heavy operation, and overlaps with what we're already doing in RecreateRenderStateContext
	// a little bit.  Ideally we do everything in a single pass.  Furthermore, if this could be updated lazily it would be faster.
	{
		for (FObjectIterator Iter(UStaticMeshComponent::StaticClass()); Iter; ++Iter)
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(*Iter);
			if (StaticMeshComponent->GetStaticMesh() == StaticMesh)
			{
				// it needs to recreate IF it already has been created
				if (StaticMeshComponent->IsPhysicsStateCreated())
				{
					StaticMeshComponent->RecreatePhysicsState();
				}
			}
		}
	}
}

bool AreVerticesEqual(FStaticMeshBuildVertex const& A, FStaticMeshBuildVertex const& B, float ComparisonThreshold)
{
	if (   !A.Position.Equals(B.Position, ComparisonThreshold)
		|| !NormalsEqual(A.TangentX, B.TangentX)
		|| !NormalsEqual(A.TangentY, B.TangentY)
		|| !NormalsEqual(A.TangentZ, B.TangentZ)
		|| A.Color != B.Color)
	{
		return false;
	}

	// UVs
	for (int32 UVIndex = 0; UVIndex < MAX_STATIC_TEXCOORDS; UVIndex++)
	{
		if (!UVsEqual(A.UVs[UVIndex], B.UVs[UVIndex]))
		{
			return false;
		}
	}

	return true;
}

void BuildVertexBuffer(
	  UStaticMesh *StaticMesh
	, int32 LodIndex
	, UMeshDescription* MeshDescription
	, FStaticMeshLODResources& StaticMeshLOD
	, const FMeshBuildSettings& LODBuildSettings
	, TArray< uint32 >& IndexBuffer
	, TArray<int32>& OutWedgeMap
	, TArray<TArray<uint32> >& OutPerSectionIndices
	, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices
	, const TMultiMap<int32, int32>& OverlappingCorners
	, float VertexComparisonThreshold
	, TArray<int32>& RemapVerts)
{
	FVertexArray& Vertices = MeshDescription->Vertices();
	FVertexInstanceArray& VertexInstances = MeshDescription->VertexInstances();
	FPolygonGroupArray& PolygonGroupArray = MeshDescription->PolygonGroups();
	FPolygonArray& PolygonArray = MeshDescription->Polygons();
	
	OutWedgeMap.Reset();
	OutWedgeMap.AddZeroed(VertexInstances.Num());

	TArray<int32> RemapVertexInstanceID;
	// set up vertex buffer elements
	StaticMeshBuildVertices.Reserve(VertexInstances.Num());
	bool bHasColor = false;
	//Fill the remap array
	RemapVerts.AddZeroed(VertexInstances.Num());
	for (int32& RemapIndex : RemapVerts)
	{
		RemapIndex = INDEX_NONE;
	}
	TArray<int32> DupVerts;

	const uint32 NumTextureCoord = MeshDescription->VertexInstanceAttributes().GetAttributeIndexCount<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	const TPolygonGroupAttributeArray<FName>& PolygonGroupImportedMaterialSlotNames = MeshDescription->PolygonGroupAttributes().GetAttributes<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	const TVertexAttributeArray<FVector>& VertexPositions = MeshDescription->VertexAttributes().GetAttributes<FVector>( MeshAttribute::Vertex::Position );
	const TVertexInstanceAttributeArray<FVector>& VertexInstanceNormals = MeshDescription->VertexInstanceAttributes().GetAttributes<FVector>( MeshAttribute::VertexInstance::Normal );
	const TVertexInstanceAttributeArray<FVector>& VertexInstanceTangents = MeshDescription->VertexInstanceAttributes().GetAttributes<FVector>( MeshAttribute::VertexInstance::Tangent );
	const TVertexInstanceAttributeArray<float>& VertexInstanceBinormalSigns = MeshDescription->VertexInstanceAttributes().GetAttributes<float>( MeshAttribute::VertexInstance::BinormalSign );
	const TVertexInstanceAttributeArray<FVector4>& VertexInstanceColors = MeshDescription->VertexInstanceAttributes().GetAttributes<FVector4>( MeshAttribute::VertexInstance::Color );
	const TVertexInstanceAttributeIndicesArray<FVector2D>& VertexInstanceUVs = MeshDescription->VertexInstanceAttributes().GetAttributesSet<FVector2D>( MeshAttribute::VertexInstance::TextureCoordinate );

	// Set up index buffer
	for (const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
	{
		const TArray<FPolygonID>& Polygons = MeshDescription->GetPolygonGroupPolygons(PolygonGroupID);

		TArray<uint32>& SectionIndices = OutPerSectionIndices[PolygonGroupID.GetValue()];

		// Create new rendering section
		int32 SectionIndex = StaticMeshLOD.Sections.Add(FStaticMeshSection());
		FStaticMeshSection& StaticMeshSection = StaticMeshLOD.Sections.Last();

		StaticMeshSection.FirstIndex = IndexBuffer.Num();
		StaticMeshSection.NumTriangles = 0;
		for (const FPolygonID& PolygonID : Polygons)
		{
			StaticMeshSection.NumTriangles += (uint32)MeshDescription->GetPolygonTriangles(PolygonID).Num();
		}

		StaticMeshSection.MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(PolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
		
		if (!StaticMesh->StaticMaterials.IsValidIndex(StaticMeshSection.MaterialIndex))
		{
			if (StaticMesh->StaticMaterials.IsValidIndex(PolygonGroupID.GetValue()))
			{
				StaticMeshSection.MaterialIndex = PolygonGroupID.GetValue();
			}
			else
			{
				StaticMeshSection.MaterialIndex = 0;
			}
		}

		if (LodIndex > 0)
		{
			//Set the overwrite section info map
			FMeshSectionInfo SectionInfo = StaticMesh->SectionInfoMap.Get(LodIndex, SectionIndex);
			SectionInfo.MaterialIndex = StaticMeshSection.MaterialIndex;
			StaticMesh->SectionInfoMap.Set(LodIndex, SectionIndex, SectionInfo);
		}

		for (const FPolygonID PolygonID : Polygons)
		{
			const TArray<FMeshTriangle>& PolygonTriangles = MeshDescription->GetPolygonTriangles(PolygonID);
			int32 ReserveSize = IndexBuffer.Num() + PolygonTriangles.Num() * 3;
			IndexBuffer.Reserve(ReserveSize);
			uint32 MinIndex = TNumericLimits< uint32 >::Max();
			uint32 MaxIndex = TNumericLimits< uint32 >::Min();
			for (int32 TriangleIndex = 0; TriangleIndex < PolygonTriangles.Num(); ++TriangleIndex)
			{
				const FMeshTriangle& Triangle = PolygonTriangles[TriangleIndex];
				for (int32 TriVert = 0; TriVert < 3; ++TriVert)
				{
					const FVertexInstanceID VertexInstanceID = Triangle.GetVertexInstanceID(TriVert);
					const int32 VertexInstanceValue = VertexInstanceID.GetValue();

					const FVertexID VertexID = MeshDescription->GetVertexInstanceVertex(VertexInstanceID);

					const FVector& VertexPosition = VertexPositions[VertexID];
					const FVector& VertexInstanceNormal = VertexInstanceNormals[VertexInstanceID];
					const FVector& VertexInstanceTangent = VertexInstanceTangents[VertexInstanceID];
					const float VertexInstanceBinormalSign = VertexInstanceBinormalSigns[VertexInstanceID];
					const FVector4& VertexInstanceColor = VertexInstanceColors[VertexInstanceID];

					const FLinearColor LinearColor(VertexInstanceColor);
					if (LinearColor != FLinearColor::White)
					{
						bHasColor = true;
					}

					FStaticMeshBuildVertex StaticMeshVertex;

					StaticMeshVertex.Position = VertexPosition * LODBuildSettings.BuildScale3D;
					const FMatrix ScaleMatrix = FScaleMatrix(LODBuildSettings.BuildScale3D).Inverse().GetTransposed();
					StaticMeshVertex.TangentX = ScaleMatrix.TransformVector(VertexInstanceTangent).GetSafeNormal();
					StaticMeshVertex.TangentY = ScaleMatrix.TransformVector(FVector::CrossProduct(VertexInstanceNormal, VertexInstanceTangent).GetSafeNormal() * VertexInstanceBinormalSign).GetSafeNormal();
					StaticMeshVertex.TangentZ = ScaleMatrix.TransformVector(VertexInstanceNormal).GetSafeNormal();
					StaticMeshVertex.Color = LinearColor.ToFColor(true);
					const uint32 MaxNumTexCoords = FMath::Min<int32>(MAX_MESH_TEXTURE_COORDS_MD, MAX_STATIC_TEXCOORDS);
					for (uint32 UVIndex = 0; UVIndex < MaxNumTexCoords; ++UVIndex)
					{
						if(UVIndex < NumTextureCoord)
						{
							StaticMeshVertex.UVs[UVIndex] = VertexInstanceUVs.GetArrayForIndex(UVIndex)[VertexInstanceID];
						}
						else
						{
							StaticMeshVertex.UVs[UVIndex] = FVector2D(0.0f, 0.0f);
						}
					}
					

					//Never add duplicated vertex instance
					DupVerts.Reset();
					OverlappingCorners.MultiFind(VertexInstanceValue, DupVerts);
					int32 Index = INDEX_NONE;
					for (int32 k = 0; k < DupVerts.Num(); k++)
					{
						int32 Location = RemapVerts.IsValidIndex(DupVerts[k]) ? RemapVerts[DupVerts[k]] : INDEX_NONE;
						if (Location != INDEX_NONE && AreVerticesEqual(StaticMeshVertex, StaticMeshBuildVertices[Location], VertexComparisonThreshold))
						{
							Index = Location;
							break;
						}
					}
					if (Index == INDEX_NONE)
					{
						Index = StaticMeshBuildVertices.Add(StaticMeshVertex);
					}
					RemapVerts[VertexInstanceValue] = Index;
					const uint32 RenderingVertexIndex = RemapVerts[VertexInstanceValue];
					IndexBuffer.Add(RenderingVertexIndex);
					OutWedgeMap[VertexInstanceValue] = RenderingVertexIndex;
					SectionIndices.Add(RenderingVertexIndex);
					MinIndex = FMath::Min(MinIndex, RenderingVertexIndex);
					MaxIndex = FMath::Max(MaxIndex, RenderingVertexIndex);
				}
			}
			StaticMeshSection.MinVertexIndex = MinIndex;
			StaticMeshSection.MaxVertexIndex = MaxIndex;
		}

		if (Polygons.Num() == 0)
		{
			// No triangles in this section
			StaticMeshSection.MinVertexIndex = 0;
			StaticMeshSection.MaxVertexIndex = 0;
		}
	}

	//Optimize before setting the buffer
	if (VertexInstances.Num() < 100000 * 3)
	{
		BuildOptimizationHelper::CacheOptimizeVertexAndIndexBuffer(StaticMeshBuildVertices, OutPerSectionIndices);
	}

	StaticMeshLOD.VertexBuffer.SetUseHighPrecisionTangentBasis(LODBuildSettings.bUseHighPrecisionTangentBasis);
	StaticMeshLOD.VertexBuffer.SetUseFullPrecisionUVs(LODBuildSettings.bUseFullPrecisionUVs);
	StaticMeshLOD.VertexBuffer.Init(StaticMeshBuildVertices, NumTextureCoord);
	StaticMeshLOD.PositionVertexBuffer.Init(StaticMeshBuildVertices);
	if (bHasColor)
	{
		StaticMeshLOD.ColorVertexBuffer.Init(StaticMeshBuildVertices);
	}
	else
	{
		StaticMeshLOD.ColorVertexBuffer.InitFromSingleColor(FColor::White, StaticMeshBuildVertices.Num());
	}
}

void BuildAllBufferOptimizations(FStaticMeshLODResources& StaticMeshLOD, const FMeshBuildSettings& LODBuildSettings, TArray< uint32 >& IndexBuffer, bool bNeeds32BitIndices, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices)
{
	const EIndexBufferStride::Type IndexBufferStride = bNeeds32BitIndices ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit;

	// Build the reversed index buffer.
	if (LODBuildSettings.bBuildReversedIndexBuffer)
	{
		TArray<uint32> InversedIndices;
		const int32 IndexCount = IndexBuffer.Num();
		InversedIndices.AddUninitialized(IndexCount);

		for (int32 SectionIndex = 0; SectionIndex < StaticMeshLOD.Sections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& SectionInfo = StaticMeshLOD.Sections[SectionIndex];
			const int32 SectionIndexCount = SectionInfo.NumTriangles * 3;

			for (int32 i = 0; i < SectionIndexCount; ++i)
			{
				InversedIndices[SectionInfo.FirstIndex + i] = IndexBuffer[SectionInfo.FirstIndex + SectionIndexCount - 1 - i];
			}
		}
		StaticMeshLOD.ReversedIndexBuffer.SetIndices(InversedIndices, IndexBufferStride);
	}

	// Build the depth-only index buffer.
	TArray<uint32> DepthOnlyIndices;
	{
		BuildOptimizationHelper::BuildDepthOnlyIndexBuffer(
			DepthOnlyIndices,
			StaticMeshBuildVertices,
			IndexBuffer,
			StaticMeshLOD.Sections
		);

		if (DepthOnlyIndices.Num() < 50000 * 3)
		{
			BuildOptimizationHelper::CacheOptimizeIndexBuffer(DepthOnlyIndices);
		}

		StaticMeshLOD.DepthOnlyIndexBuffer.SetIndices(DepthOnlyIndices, IndexBufferStride);
	}

	// Build the inversed depth only index buffer.
	if (LODBuildSettings.bBuildReversedIndexBuffer)
	{
		TArray<uint32> ReversedDepthOnlyIndices;
		const int32 IndexCount = DepthOnlyIndices.Num();
		ReversedDepthOnlyIndices.AddUninitialized(IndexCount);
		for (int32 i = 0; i < IndexCount; ++i)
		{
			ReversedDepthOnlyIndices[i] = DepthOnlyIndices[IndexCount - 1 - i];
		}
		StaticMeshLOD.ReversedDepthOnlyIndexBuffer.SetIndices(ReversedDepthOnlyIndices, IndexBufferStride);
	}

	// Build a list of wireframe edges in the static mesh.
	{
		TArray<BuildOptimizationHelper::FMeshEdge> Edges;
		TArray<uint32> WireframeIndices;

		BuildOptimizationHelper::FStaticMeshEdgeBuilder(IndexBuffer, StaticMeshBuildVertices, Edges).FindEdges();
		WireframeIndices.Empty(2 * Edges.Num());
		for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); EdgeIndex++)
		{
			BuildOptimizationHelper::FMeshEdge&	Edge = Edges[EdgeIndex];
			WireframeIndices.Add(Edge.Vertices[0]);
			WireframeIndices.Add(Edge.Vertices[1]);
		}
		StaticMeshLOD.WireframeIndexBuffer.SetIndices(WireframeIndices, IndexBufferStride);
	}

	// Build the adjacency index buffer used for tessellation.
	if (LODBuildSettings.bBuildAdjacencyBuffer)
	{
		TArray<uint32> AdjacencyIndices;

		BuildOptimizationHelper::BuildStaticAdjacencyIndexBuffer(
			StaticMeshLOD.PositionVertexBuffer,
			StaticMeshLOD.VertexBuffer,
			IndexBuffer,
			AdjacencyIndices
		);
		StaticMeshLOD.AdjacencyIndexBuffer.SetIndices(AdjacencyIndices, IndexBufferStride);
	}
}