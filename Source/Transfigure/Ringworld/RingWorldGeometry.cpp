// RingWorldGeometry.cpp

#include "RingWorldGeometry.h"

double URingWorldGeometry::GetCircumference(const FRingWorldSpec& Spec)
{
    return 2.0 * UE_DOUBLE_PI * Spec.Radius;
}

double URingWorldGeometry::GetAngularVelocity(const FRingWorldSpec& Spec)
{
    // a = omega^2 * R  ->  omega = sqrt(a / R)
    // Units cancel cleanly: (cm/s^2) / cm = 1/s^2.
    if (Spec.Radius <= 0.0)
    {
        return 0.0;
    }
    return FMath::Sqrt(Spec.TargetGravity / Spec.Radius);
}

double URingWorldGeometry::GetTangentialSpeed(const FRingWorldSpec& Spec)
{
    return GetAngularVelocity(Spec) * Spec.Radius;
}

double URingWorldGeometry::GetSpinPeriod(const FRingWorldSpec& Spec)
{
    const double Omega = GetAngularVelocity(Spec);
    return (Omega > 0.0) ? (2.0 * UE_DOUBLE_PI / Omega) : 0.0;
}

double URingWorldGeometry::GetGravityAtAltitude(const FRingWorldSpec& Spec, double Altitude)
{
    const double Omega = GetAngularVelocity(Spec);
    const double LocalRadius = FMath::Max(Spec.Radius - Altitude, 0.0);
    return Omega * Omega * LocalRadius;
}

double URingWorldGeometry::WrapArc(const FRingWorldSpec& Spec, double Arc)
{
    const double C = GetCircumference(Spec);
    if (C <= 0.0)
    {
        return 0.0;
    }
    double Wrapped = FMath::Fmod(Arc, C);
    if (Wrapped < 0.0)
    {
        Wrapped += C;
    }
    return Wrapped;
}

double URingWorldGeometry::ShortestArcDelta(const FRingWorldSpec& Spec, double From, double To)
{
    const double C = GetCircumference(Spec);
    if (C <= 0.0)
    {
        return 0.0;
    }
    double Delta = WrapArc(Spec, To - From);
    if (Delta > C * 0.5)
    {
        Delta -= C;
    }
    return Delta;
}

double URingWorldGeometry::ArcToLongitudeDegrees(const FRingWorldSpec& Spec, double Arc)
{
    const double C = GetCircumference(Spec);
    return (C > 0.0) ? (WrapArc(Spec, Arc) / C) * 360.0 : 0.0;
}

FVector URingWorldGeometry::RingToWorld(const FRingWorldSpec& Spec, const FRingCoords& Coords)
{
    if (Spec.Radius <= 0.0)
    {
        return FVector::ZeroVector;
    }

    const double Theta = Coords.Arc / Spec.Radius;
    const double LocalRadius = Spec.Radius - Coords.Altitude;

    return FVector(
        LocalRadius * FMath::Sin(Theta),
        Coords.Lateral,
        -LocalRadius * FMath::Cos(Theta));
}

FRingCoords URingWorldGeometry::WorldToRing(const FRingWorldSpec& Spec, const FVector& WorldPos)
{
    const double LocalRadius = FMath::Sqrt(WorldPos.X * WorldPos.X + WorldPos.Z * WorldPos.Z);

    // atan2(X, -Z) puts theta = 0 at world (0, *, -R), matching RingToWorld.
    const double Theta = FMath::Atan2(WorldPos.X, -WorldPos.Z);

    FRingCoords Out;
    Out.Arc      = WrapArc(Spec, Theta * Spec.Radius);
    Out.Lateral  = WorldPos.Y;
    Out.Altitude = Spec.Radius - LocalRadius;
    return Out;
}

FVector URingWorldGeometry::GetGravityDirection(const FRingWorldSpec& Spec, const FVector& WorldPos)
{
    // "Down" is radially outward from the spin axis, which lies along +Y.
    FVector Radial(WorldPos.X, 0.0, WorldPos.Z);

    if (Radial.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
    {
        // Exactly on the axis. No meaningful direction; pick the theta = 0 answer.
        return FVector(0.0, 0.0, -1.0);
    }
    return Radial.GetUnsafeNormal();
}

FTransform URingWorldGeometry::GetSurfaceFrame(const FRingWorldSpec& Spec, const FRingCoords& Coords)
{
    if (Spec.Radius <= 0.0)
    {
        return FTransform::Identity;
    }

    const double Theta = Coords.Arc / Spec.Radius;
    const double S = FMath::Sin(Theta);
    const double C = FMath::Cos(Theta);

    // At theta = 0 this reduces to the world identity basis.
    const FVector Forward(C, 0.0, S);   // increasing Arc, spinward
    const FVector Right(0.0, 1.0, 0.0); // increasing Lateral
    const FVector Up(-S, 0.0, C);       // toward the spin axis

    const FVector Position = RingToWorld(Spec, Coords);

    const FMatrix Basis(
        FPlane(Forward, 0.0),
        FPlane(Right, 0.0),
        FPlane(Up, 0.0),
        FPlane(Position, 1.0));

    return FTransform(Basis);
}

double URingWorldGeometry::GetCurvatureRise(const FRingWorldSpec& Spec, double AlongRingDistance)
{
    if (Spec.Radius <= 0.0)
    {
        return 0.0;
    }
    return Spec.Radius - Spec.Radius * FMath::Cos(AlongRingDistance / Spec.Radius);
}

double URingWorldGeometry::GetCurvatureRiseApprox(const FRingWorldSpec& Spec, double AlongRingDistance)
{
    if (Spec.Radius <= 0.0)
    {
        return 0.0;
    }
    return (AlongRingDistance * AlongRingDistance) / (2.0 * Spec.Radius);
}

FVector URingWorldGeometry::GetCoriolisAcceleration(const FRingWorldSpec& Spec, const FVector& RelativeVelocity)
{
    // Spin axis is +Y. The sign is negative so that the surface travels in the
    // +Arc (spinward, +X at theta = 0) direction.
    const FVector OmegaVec(0.0, -GetAngularVelocity(Spec), 0.0);

    return -2.0 * FVector::CrossProduct(OmegaVec, RelativeVelocity);
}

bool URingWorldGeometry::IsAboveOrbitalHandoff(const FRingWorldSpec& Spec, double Altitude)
{
    return Altitude >= Spec.OrbitalHandoffAltitude;
}
