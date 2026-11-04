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

void FLimbMeshGenerator::BuildTube(const FLimbShape& S, FLimbMesh& Out)
{
    Out = FLimbMesh();
    const int32 Rings = FMath::Max(S.Rings, 3);
    const int32 Sides = FMath::Max(S.Sides, 3);
    const FVector SeedOffset(S.NoiseSeed * 0.731f, S.NoiseSeed * 1.317f, S.NoiseSeed * 0.193f);

    // Start frame: any vector perpendicular to the first tangent.
    FVector Tangent = SpineTangent(S, 0.f);
    FVector Normal = FVector::CrossProduct(Tangent, FMath::Abs(Tangent.Z) < 0.9f ? FVector::UpVector : FVector::RightVector).GetSafeNormal();

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

        // Rounded ends: radius goes to 0 at t=0 and t=1 with a blunt profile.
        const float Cap = FMath::Pow(FMath::Sin(PI * T), 0.35f);
        const float BaseRadius = FMath::Lerp(S.StartRadius, S.EndRadius, T) + S.Bulge * FMath::Sin(PI * T);

        for (int32 Side = 0; Side <= Sides; ++Side)   // last column duplicates the first for clean UVs
        {
            const float A = 2.f * PI * (Side % Sides) / Sides;
            const FVector Dir = Normal * FMath::Cos(A) + Binormal * FMath::Sin(A);

            // Seamless noise: sample 3D Perlin on a circle, moving along the spine.
            const FVector NoisePos = FVector(FMath::Cos(A), FMath::Sin(A), T * 3.f) * 1.7f + SeedOffset;
            const float Lump = S.Lumpiness * FMath::PerlinNoise3D(NoisePos);

            const float Radius = FMath::Max(0.f, (BaseRadius + Lump) * Cap);
            Out.Vertices.Add(Center + Dir * Radius);
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
    return S;
}
