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

void FLimbMeshGenerator::BuildTube(const FLimbShape& InShape, FLimbMesh& Out)
{
    Out = FLimbMesh();
    FLimbShape S = InShape;
    if (S.bMechanical) S.Style = FLimbShape::EStyle::Metal;
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
            const float Coarse = FMath::PerlinNoise3D(NoisePos);
            // Second, finer octave: wrinkles and sagging skin.
            const float Fine = FMath::PerlinNoise3D(NoisePos * 3.3f + FVector(17.f));
            const float Lump = S.bMechanical ? 0.f : S.Lumpiness * (Coarse + S.Wrinkle * Fine);

            const float Radius = FMath::Max(0.f, (BaseRadius + Lump) * Cap);
            const FVector Offset = Normal * (FMath::Cos(A) * S.Width) + Binormal * (FMath::Sin(A) * S.Depth);
            const FVector Pos = Center + Offset * Radius;
            Out.Vertices.Add(Pos);
            Out.UVs.Add(FVector2D(static_cast<float>(Side) / Sides, T));

            // Vertex colours: noise in the part's OWN space, so patterns stick to the body as it moves.
            const FVector P = Pos * 0.09f + SeedOffset;
            float Vein = 0.f, Mottle = 0.f, Crease = 0.f;
            switch (S.Style)
            {
            case FLimbShape::EStyle::Metal:
            {
                // Rust patches (G) and dark grime in the rib grooves (B).
                const float Rust = FMath::PerlinNoise3D(P * 1.4f) * 0.5f + 0.5f;
                Mottle = FMath::Clamp((Rust - 0.58f) * 4.f, 0.f, 1.f);
                Crease = (FMath::Frac(T * FMath::Max(S.Segments, 1)) < 0.16f) ? 0.6f : 0.f;
                break;
            }
            case FLimbShape::EStyle::Growth:
                // G = pulse phase (desyncs neighbouring sacs), B = darker toward the base.
                Mottle = FMath::Frac(S.NoiseSeed * 0.0137f + (FMath::PerlinNoise3D(P) * 0.15f));
                Crease = FMath::Clamp(1.f - T * 2.f, 0.f, 1.f) * 0.6f;
                break;
            default:
            {
                // Veins: thin ridges where a noise field crosses zero.
                const float V = FMath::Abs(FMath::PerlinNoise3D(P * 2.2f + FVector(5.f)));
                Vein = FMath::Clamp(1.f - V / 0.055f, 0.f, 1.f) * 0.85f;
                // Mottling / bruises.
                Mottle = FMath::Clamp(FMath::PerlinNoise3D(P * 0.7f) * 0.9f + 0.5f, 0.f, 1.f);
                // Creases: dents in the surface and the pinched joints.
                const float Dent = S.Lumpiness > 0.f ? FMath::Clamp(-Lump / S.Lumpiness, 0.f, 1.f) : 0.f;
                const float Joint = FMath::Exp(-FMath::Square(T / 0.1f)) + FMath::Exp(-FMath::Square((1.f - T) / 0.1f));
                Crease = FMath::Clamp(Dent * 0.7f + Joint * 0.5f, 0.f, 1.f);
                break;
            }
            }
            Out.Colors.Add(FColor(
                static_cast<uint8>(Vein * 255.f), static_cast<uint8>(Mottle * 255.f), static_cast<uint8>(Crease * 255.f), 255));
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
    S.Rings = 22;
    S.Sides = 18;

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
