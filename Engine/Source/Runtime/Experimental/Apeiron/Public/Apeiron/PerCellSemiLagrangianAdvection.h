// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Apeiron/ArrayND.h"
#include "Apeiron/UniformGrid.h"

namespace Apeiron
{
template<class T, int d>
class TPerCellSemiLagrangianAdvection
{
  public:
	TPerCellSemiLagrangianAdvection() {}
	~TPerCellSemiLagrangianAdvection() {}

	template<class T_SCALAR>
	void Apply(const TUniformGrid<T, d>& Grid, TArrayND<T_SCALAR, d>& Scalar, const TArrayND<T_SCALAR, d>& ScalarN, const TArrayFaceND<T, d>& VelocityN, const T Dt, const TVector<int32, d>& Index)
	{
		TVector<T, d> Location = Grid.Location(Index);
		TVector<T, d> X = Grid.ClampMinusHalf(Location - Dt * Grid.LinearlyInterpolate(VelocityN, Location));
		Scalar(Index) = Grid.LinearlyInterpolate(ScalarN, X);
	}
};
}
