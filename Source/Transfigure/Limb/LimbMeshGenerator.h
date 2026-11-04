// Source/Transfigure/Limb/LimbMeshGenerator.h
//
// Pure math: turns an FLimbShape into a closed, lumpy tube mesh.
//   - spine: quadratic Bezier from Start to End, bent by BendOffset
//   - rings: Sides vertices around the spine, oriented by parallel transport
//   - radius(t, angle) = lerp(StartR, EndR, t) + Bulge*sin(pi t) + Lumpiness*noise,
//     multiplied by a rounded end-cap profile so both ends close.

#pragma once

#include "CoreMinimal.h"
#include "Limb/LimbTypes.h"

class TRANSFIGURE_API FLimbMeshGenerator
{
public:
    /** Build the mesh for one part. */
    static void BuildTube(const FLimbShape& Shape, FLimbMesh& Out);

    /** Point on the part's spine at t in [0,1] (part-local space). */
    static FVector SpinePoint(const FLimbShape& Shape, float T);

    /** Roll a shape for a spec: bone-local start/end plus seeded mutations. */
    static FLimbShape MakeShape(const FLimbPartSpec& Spec, const FVector& LocalStart, const FVector& LocalEnd,
        const FLimbMutationRanges& Ranges, float BodyFatness, FRandomStream& Rng);
};
