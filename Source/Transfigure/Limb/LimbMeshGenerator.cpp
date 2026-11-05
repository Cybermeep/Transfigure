// Source/Transfigure/Limb/LimbMeshGenerator.cpp

#include "Limb/LimbMeshGenerator.h"
#include "KismetProceduralMeshLibrary.h"

FVector FLimbMeshGenerator::SpinePoint(const FLimbShape& S, float T)
{
    const FVector Control = (S.Start + S.End) * 0.5f + S.BendOffset;
    const float U = 1.f - T;
    return S.Start * (U * U) + Control * (2.f * U * T) + S.End * (T * T);
}

static FVector SpineTangent(const FLimbShape& S, float T)
{
    const FVector Control = (S.Start + S.End) * 0.5f + S.BendOffset;
    const FVector D = (Control - S.Start) * (2.f * (1.f - T)) + (S.End - Control) * (2.f * T);
    return D.GetSafeNormal(KINDA_SMALL_NUMBER, (S.End - S.Start).GetSafeNormal());
}

FVector FLimbMeshGenerator::SpineDirection(const FLimbShape& S, float T)
{
    return SpineTangent(S, T);
}

/** Radius profile without noise or cap: taper + off-centre bulge, pinched at the joints, ribbed if mechanical. */
static float ProfileRadius(const FLimbShape& S, float T)
{
    float R = FMath::Lerp(S.StartRadius, S.EndRadius, T);
    if (S.bMechanical)
    {
        // Ribbed piston segments: a raised collar at each segment boundary.
        const float F = FMath::Frac(T * FMath::Max(S.Segments, 1));
        return R * ((F < 0.12f || F > 0.88f) ? 1.15f : 1.f);
    }
    const float B = (T - S.BulgeCenter) / 0.28f;
    R += S.Bulge * FMath::Exp(-B * B);
    const float PinchA = FMath::Exp(-FMath::Square(T / 0.12f));
    const float PinchB = FMath::Exp(-FMath::Square((1.f - T) / 0.12f));
    return R * (1.f - S.Pinch * (PinchA + PinchB));
}

float FLimbMeshGenerator::RadiusAt(const FLimbShape& S, float T)
{
    return ProfileRadius(S, T) * 0.5f * (S.Width + S.Depth);
}

void FLimbMeshGenerator::BuildTube(const FLimbShape& S, FLimbMesh& Out)
{
    Out = FLimbMesh();
    const int32 Rings = FMath::Max(S.bMechanical ? S.Segments * 6 : S.Rings, 3);
    const int32 Sides = FMath::Max(S.bMechanical ? 6 : S.Sides, 3);
    const FVector SeedOffset(S.NoiseSeed * 0.731f, S.NoiseSeed * 1.317f, S.NoiseSeed * 0.193f);

    // Start frame: any vector perpendicular to the first tangent.
    FVector Tangent = SpineTangent(S, 0.f);
    // Start frame: aligned with WideAxis if given (so "Width" runs left-right on a torso).
    FVector Normal = FVector::VectorPlaneProject(S.WideAxis, Tangent).GetSafeNormal();
    if (Normal.IsNearlyZero())
    {
        Normal = FVector::CrossProduct(Tangent, FMath::Abs(Tangent.Z) < 0.9f ? FVector::UpVector : FVector::RightVector).GetSafeNormal();
    }

    for (int32 R = 0; R <= Rings; ++R)
    {
        const float T = static_cast<float>(R) / Rings;
        const FVector Center = SpinePoint(S, T);

        // Parallel transport: rotate the normal by the change in tangent (no twisting).
        const FVector NewTangent = SpineTangent(S, T);
        const FQuat Turn = FQuat::FindBetweenNormals(Tangent, NewTangent);
        Normal = Turn.RotateVector(Normal).GetSafeNormal();
        Tangent = NewTangent;
        const FVector Binormal = FVector::CrossProduct(Tangent, Normal);

        // Closed ends: radius goes to 0 at t=0 and t=1 (CapExponent sets how blunt).
        const float Cap = FMath::Pow(FMath::Sin(PI * T), S.CapExponent);
        const float BaseRadius = ProfileRadius(S, T);

        for (int32 Side = 0; Side <= Sides; ++Side)   // last column duplicates the first for clean UVs
        {
            const float A = 2.f * PI * (Side % Sides) / Sides;

            // Seamless noise: sample 3D Perlin on a circle, moving along the spine.
            const FVector NoisePos = FVector(FMath::Cos(A), FMath::Sin(A), T * 3.f) * 1.7f + SeedOffset;
            const float Lump = S.bMechanical ? 0.f : S.Lumpiness * FMath::PerlinNoise3D(NoisePos);

            const float Radius = FMath::Max(0.f, (BaseRadius + Lump) * Cap);
            const FVector Offset = Normal * (FMath::Cos(A) * S.Width) + Binormal * (FMath::Sin(A) * S.Depth);
            Out.Vertices.Add(Center + Offset * Radius);
            Out.UVs.Add(FVector2D(static_cast<float>(Side) / Sides, T));
        }
    }

    // Triangles, wound so the face normal points away from the spine (outward).
    const int32 Stride = Sides + 1;
    for (int32 R = 0; R < Rings; ++R)
    {
        const FVector Mid = SpinePoint(S, (R + 0.5f) / Rings);
        for (int32 Side = 0; Side < Sides; ++Side)
        {
            const int32 A = R * Stride + Side;
            const int32 B = A + 1;
            const int32 C = A + Stride;
            const int32 D = C + 1;

            auto AddTri = [&](int32 I0, int32 I1, int32 I2)
            {
                const FVector& V0 = Out.Vertices[I0];
                const FVector& V1 = Out.Vertices[I1];
                const FVector& V2 = Out.Vertices[I2];
                const FVector FaceN = FVector::CrossProduct(V1 - V0, V2 - V0);
                if (FaceN.IsNearlyZero()) return;                       // collapsed cap triangles
                const bool bOutward = FVector::DotProduct(FaceN, (V0 + V1 + V2) / 3.f - Mid) > 0.f;
                Out.Triangles.Append(bOutward ? TArray<int32>{ I0, I1, I2 } : TArray<int32>{ I0, I2, I1 });
            };
            AddTri(A, C, B);
            AddTri(B, C, D);
        }
    }

    UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Out.Vertices, Out.Triangles, Out.UVs, Out.Normals, Out.Tangents);
}

FLimbShape FLimbMeshGenerator::MakeShape(const FLimbPartSpec& Spec, const FVector& LocalStart, const FVector& LocalEnd,
    const FLimbMutationRanges& M, float BodyFatness, FRandomStream& Rng)
{
    FLimbShape S;
    const FVector Axis = (LocalEnd - LocalStart).GetSafeNormal(KINDA_SMALL_NUMBER, FVector::ForwardVector);
    const float Length = FVector::Dist(LocalStart, LocalEnd);

    const float Thick = BodyFatness * Rng.FRandRange(M.PartThickness.X, M.PartThickness.Y);
    S.StartRadius = Spec.StartRadius * Thick;
    S.EndRadius = Spec.EndRadius * Thick;
    S.Bulge = FMath::Max(0.f, Spec.Bulge + Rng.FRandRange(M.BulgeOffset.X, M.BulgeOffset.Y));
    S.Lumpiness = Rng.FRandRange(M.Lumpiness.X, M.Lumpiness.Y);
    S.NoiseSeed = Rng.RandRange(0, 100000);

    // Extend past both joints a little so neighbouring parts overlap and hide the seams.
    S.Start = LocalStart - Axis * S.StartRadius * 0.6f;
    S.End = LocalEnd + Axis * S.EndRadius * 0.6f;

    // Bend sideways in a random direction perpendicular to the part.
    const FVector Side = FVector::CrossProduct(Axis, Rng.GetUnitVector()).GetSafeNormal();
    S.BendOffset = Side * Length * Rng.FRandRange(M.Bend.X, M.Bend.Y);

    // Anatomy: muscle peaks off-centre, joints pinch in a little.
    S.BulgeCenter = Rng.FRandRange(0.32f, 0.62f);
    S.Pinch = Rng.FRandRange(0.06f, 0.16f);
    return S;
}
