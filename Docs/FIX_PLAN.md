# Transfigure Pre-Limb Fix Plan

Nov 4, 2026 · @ivy

## How to use this plan

Work through the phases in order, compiling and testing after each one, then start the limb system. Every test runs on a basic flat map with placeholder assets, so no art is needed.

| Phase | What it fixes | How you know it worked |
| --- | --- | --- |
| 0. Project and test map | Spell setup, default map, gravity on a flat floor | You stand on the floor and can place a sigil |
| 1. Player movement | Turning, sprint, jumps, wall run | Each move works on the test map |
| 2. Damage | Spell and gun damage | Shots and blasts lower enemy health |
| 3. Enemy lifecycle | Pool, pods, death, basic chase | Enemies spawn, chase, die and return |
| 4. Grapple | Targeting, lockups, combat moves | You can swing repeatedly and hit enemies |
| 5. Spells | Clap, combos, sigils, buffs, slows | Each effect fires once and ends correctly |

**What was checked:** the code comes from reading the `Transfigure` repo's C++ source and config. None of it has been compiled or run, so expect small compile fixes. Engine API details are from memory of UE 5.6.

**What wasn't checked:** the Blueprints and maps are Git LFS pointers I couldn't open. If a Blueprint already does one of these jobs, keep one version and remove the other. Likely overlaps are damage events, `InitializePool` and `InitializePod`.

**Assets assumed:** engine cubes for the level, plus the UE5 Mannequin for enemies. Nothing here depends on finished models or animations.

## Phase 0: Project setup and test map

Fix the config first and add a flat gravity switch, so a basic map behaves like a normal level. Without the switch, a floor near the world origin pulls the player upward.

### 1. Build the test map

1. Create a new level from the **Basic** template and save it as `/Game/Maps/L_TestArena`.
2. Scale the floor to about 100 x 100 units of scale, so there's room to sprint and swing.
3. Add a cube about 2,000 cm long and 500 cm tall for wall running.
4. Add a `NavMeshBoundsVolume` over the whole floor. Press **P** to confirm the green nav area appears.
5. Keep the Player Start above the floor. Don't place `ARingBandGenerator` in this map.

### 2. Correct the project config

In `Config/DefaultEngine.ini`, replace the map and Game Instance lines under `[/Script/EngineSettings.GameMapsSettings]`. The Game Instance line currently points at the native class, which leaves `SpellRegistry` empty.

```ini
GameDefaultMap=/Game/Maps/L_TestArena.L_TestArena
EditorStartupMap=/Game/Maps/L_TestArena.L_TestArena
GameInstanceClass=/Game/BP_TransfigurationGameInstance.BP_TransfigurationGameInstance_C
```

In the same file, add a trace channel for weapons and grapples:

```ini
[/Script/Engine.CollisionProfile]
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel1,DefaultResponse=ECR_Block,bTraceType=True,bStaticObject=False,Name="Weapon")
```

In `Source/Transfigure/Transfigure.h`, add a readable name for it below the existing includes:

```cpp
#define ECC_Weapon ECC_GameTraceChannel1
```

In `Transfigure.Build.cs`, change the `"RingWorld"` include path to `"Ringworld"` and remove the `"City"` line. The folder name is wrong on case-sensitive systems, and `City` doesn't exist.

### 3. Add flat gravity for testing

Add the switch to `FRingWorldSpec` in `RingWorldGeometry.h`. Every gravity caller already reads this struct, so nothing else needs a special case.

```cpp
/** Treat the level as flat: down is -Z and altitude is world Z. */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RingWorld")
bool bFlatWorldForTesting = false;
```

Add these early returns at the top of the matching functions in `RingWorldGeometry.cpp`:

```cpp
// GetGravityAtAltitude
if (Spec.bFlatWorldForTesting) return Spec.TargetGravity;

// RingToWorld
if (Spec.bFlatWorldForTesting) return FVector(Coords.Arc, Coords.Lateral, Coords.Altitude);

// WorldToRing
if (Spec.bFlatWorldForTesting) return FRingCoords(WorldPos.X, WorldPos.Y, WorldPos.Z);

// GetGravityDirection
if (Spec.bFlatWorldForTesting) return FVector(0.0, 0.0, -1.0);

// GetSurfaceFrame
if (Spec.bFlatWorldForTesting) return FTransform(RingToWorld(Spec, Coords));
```

Then replace the spec line in `Config/DefaultGame.ini`:

```ini
Spec=(Radius=160000000.000000,Width=20000000.000000,TargetGravity=981.000000,RimWallHeight=10000000.000000,OrbitalHandoffAltitude=4000000.000000,bFlatWorldForTesting=True)
```

These are the code's default ring dimensions. The current file has a 1 km radius with 100 km rim walls, which pass through the ring's axis. If you want a small ring, keep `RimWallHeight` below `Radius`.

### 4. Make the packaged build compile

`DrawDebugInfo` is declared only for editor builds but defined for all builds. Wrap the whole function in `EnemyPod.cpp`, starting at line 782:

```cpp
#if WITH_EDITOR
void AEnemyPod::DrawDebugInfo()
{
    // existing body, without its inner #if WITH_EDITOR
}
#endif
```

### 5. Check the Blueprints

- `BP_TransfigurationGameInstance`: **Spell Registry** is set to `DA_SpellRegistry`.
- `DA_SpellRegistry`: the table has a key named exactly `FireBlast`, pointing at `DA_FireBlast`. `OnDrawSigilPressed` hardcodes that name.
- `DA_FireBlast`: **Effect Class** is `BP_FireBlastEffect`. Turn **Causes Environmental Destruction** off for now.
- `GM_Transfiguration`: **Default Pawn Class** is `BP_TMCharacter`.
- `BP_TMCharacter`: the input mapping context and actions are assigned. Set **Camera Pitch Target** in the Construction Script, since it holds a component reference.

In `DestructionManager.h`, set `ChunkPoolSize = 0` until that system is rebuilt. Otherwise the first call to `GetInstance` spawns 100 physics actors that have no mesh.

**Test:** Press Play. You should spawn in `L_TestArena`, fall onto the floor and stay there. The `SpellID not in registry` warning should no longer appear when you start drawing a sigil.

## Phase 1: Player movement

Fix the rotation first, because every movement state depends on the actor's forward vector. Then separate jumping from falling, which removes the accidental jumps.

### 1. Rotation

The current basis builds its Right vector backward, which mirrors the matrix. Replace lines 109-111 of `TMCharacter.cpp`:

```cpp
SetActorRotation(FRotationMatrix::MakeFromXZ(CachedFacingForward, CachedRingUp).ToQuat());
```

Replace lines 61-63 of `BaseEnemy.cpp` the same way:

```cpp
SetActorRotation(FRotationMatrix::MakeFromXZ(Forward, CachedRingUp).ToQuat());
```

In `ATMCharacter::BeginPlay`, add this line before the first transition so the player keeps the Player Start's facing:

```cpp
CachedFacingForward = GetActorForwardVector();
```

### 2. Sprint

Nothing sets `bSprintHeld`, so sprint ends on its next tick. Replace both handlers in `TMCharacter.cpp`:

```cpp
void ATMCharacter::OnSprintStarted()
{
    bSprintHeld = true;
    if (GetCharacterMovement()->IsMovingOnGround())
    {
        TransitionTo<TMState_Sprint>();
    }
}

void ATMCharacter::OnSprintEnded()
{
    bSprintHeld = false;
    if (CurrentStateName == "Sprint")
    {
        TransitionTo<TMState_Walk>();
    }
}
```

Replace `TMState_Sprint::Tick`, and include `States/TMState_Idle.h`:

```cpp
void TMState_Sprint::Tick(float DeltaTime)
{
    auto* CMC = Owner->GetCharacterMovement();

    if (!CMC->IsMovingOnGround())
    {
        Owner->TransitionTo<TMState_Fall>();
        return;
    }
    if (!Owner->bSprintHeld)
    {
        Owner->TransitionTo<TMState_Walk>();
        return;
    }
    if (URingFrameUtils::GetPlanarSpeed(Owner) < 10.f)
    {
        Owner->TransitionTo<TMState_Idle>();
    }
}
```

Add `#include "RingFrameUtils.h"` to that file as well. `TMState_Jump.h` is already included there.

### 3. Split jumping from falling

`TMState_Jump::Enter` always launches the player. Walking off a ledge, leaving a wall and landing from a double jump all reuse it, so each one causes a jump.

Replace the class in `TMState_Jump.h`:

```cpp
class TMState_Jump : public TMStateBase
{
public:
    explicit TMState_Jump(ATMCharacter* InOwner) : TMStateBase(InOwner) {}
    virtual void Enter() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Exit() override;

protected:
    // Stops a queued launch from being read as "still grounded".
    float StateTime = 0.f;
};

class TMState_Fall : public TMState_Jump
{
public:
    using TMState_Jump::TMState_Jump;
    virtual void Enter() override;
};
```

In `TMState_Jump.cpp`, add the fall state and the grounded guard:

```cpp
void TMState_Fall::Enter()
{
    Owner->SetStateName("Fall");
}

void TMState_Jump::Tick(float DeltaTime)
{
    auto* CMC = Owner->GetCharacterMovement();
    StateTime += DeltaTime;

    if (StateTime > 0.1f && CMC->IsMovingOnGround())
    {
        Owner->JumpsRemaining = 2;
        Owner->TransitionTo<TMState_Walk>();
        return;
    }

    if (Owner->JumpsRemaining < 1)
    {
        Owner->TransitionTo<TMState_DoubleJump>();
        return;
    }

    if (TMState_WallRun::CanEnter(Owner))
    {
        Owner->TransitionTo<TMState_WallRun>();
    }
}
```

Then change these transitions:

| File and line | Change to | Also add |
| --- | --- | --- |
| `TMState_Idle.cpp:32` | `TMState_Fall` |  |
| `TMState_Walk.cpp:37` | `TMState_Fall` |  |
| `TMState_WallRun.cpp:90` | `TMState_Fall` |  |
| `TMState_DoubleJump.cpp:30` | `TMState_Walk` | `#include "States/TMState_Walk.h"` |

Wall jump also loses its saved double jump. In `TMState_WallJump::Tick`, add this after the `MinAirTime` check, and include `States/TMState_DoubleJump.h`:

```cpp
if (Owner->JumpsRemaining < 1)
{
    Owner->TransitionTo<TMState_DoubleJump>();
    return;
}
```

### 4. Wall run

The entry check requires ground within 100 cm of the actor's center, which blocks almost every wall run. No wall can be damaged yet, so allow them all until destruction is rebuilt:

```cpp
bool ATMCharacter::CanWallRunAtLocation(FVector Location) const
{
    return true;
}
```

In `TMState_WallRun::CanEnter`, add this first so a swing can't turn into a wall run:

```cpp
if (Char->IsGrappling()) return false;
```

The cross product also pushes the player backward. Replace the `WallForward` calculation and velocity assignment in `TMState_WallRun::Tick`:

```cpp
auto* CMC = Owner->GetCharacterMovement();
const FVector Up = Owner->GetRingUp();

FVector WallForward = URingFrameUtils::StripVertical(
    FVector::VectorPlaneProject(CMC->Velocity, WallNormal), Up).GetSafeNormal();

if (WallForward.IsNearlyZero())
{
    WallForward = URingFrameUtils::StripVertical(
        FVector::VectorPlaneProject(Owner->GetActorForwardVector(), WallNormal), Up).GetSafeNormal();
}

CMC->Velocity = WallForward * Owner->WallRunSpeed;
```

Wall jump should launch away from the wall. Replace the launch calculation in `TMState_WallJump::Enter`, and include `States/TMState_WallRun.h`:

```cpp
const FVector Up = Owner->GetRingUp();
FVector WallNormal;
bool bWallOnLeft = false;

const FVector Push = TMState_WallRun::DetectWall(Owner, WallNormal, bWallOnLeft)
    ? WallNormal * Owner->WallJumpLateralBoost
    : -Owner->GetActorForwardVector() * Owner->WallJumpLateralBoost;

const FVector Planar = URingFrameUtils::StripVertical(Owner->GetVelocity(), Up);
const FVector LaunchVelocity = URingFrameUtils::SetVertical(Planar + Push, Up, Owner->JumpZVelocity);

Owner->LaunchCharacter(LaunchVelocity, true, true);
Owner->JumpsRemaining = 1;
```

**Test:**

- Moving the mouse sideways turns the player, and W moves where you're facing.
- Holding sprint keeps you at sprint speed until you release it.
- Walking off a ledge drops you with no upward hop.
- Landing from a double jump doesn't launch you again.
- Jumping beside the wall while moving starts a forward wall run, and jumping off it pushes you away.

## Phase 2: Damage

Send engine damage and gun hits through `TakePoolDamage`, so one function handles all enemy damage. The limb system will later override that function to find which body part was hit.

If `BP_BaseEnemy` already uses **Event AnyDamage** to call `TakePoolDamage`, remove that graph, or spells will hit twice.

### 1. Receive engine damage on enemies

FireBlast uses `ApplyRadialDamage`, but `ABaseEnemy` never turns that into health loss. Add this to `BaseEnemy.h`:

```cpp
virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser) override;
```

Add this to `BaseEnemy.cpp`, and include `Engine/DamageEvents.h`:

```cpp
float ABaseEnemy::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    if (Applied <= 0.f) return 0.f;

    FVector HitLocation = GetActorLocation();
    if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        HitLocation = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
    }
    else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
    {
        HitLocation = static_cast<const FRadialDamageEvent&>(DamageEvent).Origin;
    }

    TakePoolDamage(Applied, HitLocation);
    return Applied;
}
```

### 2. Add the weapon to the player

In `TMCharacter.h`, forward declare `class UWeaponComponent;`, then add:

```cpp
// Public, with the other components
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
TObjectPtr<UWeaponComponent> WeaponComponent;

// Public, with the buff functions
UFUNCTION(BlueprintPure, Category = "TM|Magic|Buffs")
float GetDamageMultiplier() const { return CurrentDamageMultiplier; }

// Private, with the input handlers
void OnFirePressed();
```

In `TMCharacter.cpp`, include `Combat/WeaponComponent.h` and add:

```cpp
// Constructor
WeaponComponent = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));

// SetupPlayerInputComponent
if (IA_Fire)
{
    EIC->BindAction(IA_Fire, ETriggerEvent::Started, this, &ATMCharacter::OnFirePressed);
}

// New handler
void ATMCharacter::OnFirePressed()
{
    if (WeaponComponent) WeaponComponent->FireWeapon();
}
```

Create an `IA_Fire` input action, map it to the left mouse button if that's free, and assign it in `BP_TMCharacter`.

### 3. Fire and apply the hit

The old `ProcessHit` expects its caller to already know the damage and whether the shot killed. Replace its declaration in `WeaponComponent.h`, and add the new settings:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
float BaseDamage = 25.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
float MaxRange = 10000.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
float HeadshotMultiplier = 2.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
FName HeadBoneName = TEXT("head");

UFUNCTION(BlueprintCallable, Category = "Weapon")
bool FireWeapon();

UFUNCTION(BlueprintCallable, Category = "Weapon")
void ProcessHit(const FHitResult& Hit, float HitDamage);
```

In `WeaponComponent.cpp`, include `Transfigure.h` and `DrawDebugHelpers.h`. Replace `ProcessHit` and add `FireWeapon`. You can also delete `ApplyElementalDamage` and the commented-out blocks below it, removing its declaration from the header.

```cpp
bool UWeaponComponent::FireWeapon()
{
    if (!OwnerCharacter || !OwnerCharacter->GetController()) return false;

    FVector CamLoc;
    FRotator CamRot;
    OwnerCharacter->GetController()->GetPlayerViewPoint(CamLoc, CamRot);
    const FVector TraceEnd = CamLoc + CamRot.Vector() * MaxRange;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(WeaponFire), false, OwnerCharacter);
    FHitResult Hit;
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, TraceEnd, ECC_Weapon, Params);

#if !UE_BUILD_SHIPPING
    DrawDebugLine(GetWorld(), CamLoc, bHit ? Hit.ImpactPoint : TraceEnd, FColor::Yellow, false, 0.5f);
#endif

    if (bHit) ProcessHit(Hit, BaseDamage);
    return bHit;
}

void UWeaponComponent::ProcessHit(const FHitResult& Hit, float HitDamage)
{
    if (!OwnerCharacter) return;

    ABaseEnemy* Enemy = Cast<ABaseEnemy>(Hit.GetActor());
    if (!Enemy || !Enemy->IsAlive()) return;

    const bool bHeadshot = Hit.BoneName == HeadBoneName;
    float Damage = HitDamage * OwnerCharacter->GetDamageMultiplier();
    if (bHeadshot) Damage *= HeadshotMultiplier;

    if (bIsInfused)
    {
        if (const float* Multiplier = ElementalDamageMultipliers.Find(CurrentInfusion))
        {
            Damage *= *Multiplier;
        }
    }

    Enemy->TakePoolDamage(Damage, Hit.ImpactPoint);
    const bool bKill = !Enemy->IsAlive();

    float ManaToAdd = bKill ? ManaPerKill : ManaPerHit;
    if (bHeadshot) ManaToAdd += ManaPerHeadshot;
    OwnerCharacter->AddMana(ManaToAdd);

    FWeaponHitInfo HitInfo;
    HitInfo.HitLocation = Hit.ImpactPoint;
    HitInfo.HitActor = Enemy;
    HitInfo.bWasHeadshot = bHeadshot;
    HitInfo.bWasKill = bKill;
    HitInfo.DamageDealt = Damage;
    OnWeaponHit.Broadcast(HitInfo);

    if (bKill) OnWeaponKill.Broadcast(Enemy);
}
```

The trace hits the enemy's capsule for now, so headshots won't register until the limb system adds body-part hits. The yellow debug line stands in for a gun model and tracer.

### 4. Let infusions end

`OnInfusionExpired` never clears `bIsInfused`. Add these lines to it:

```cpp
bIsInfused = false;
InfusionTimeRemaining = 0.f;
```

### 5. Make damage buffs count for spells

The gun now reads the damage buff. In `UTransfigurationEffectBase::GetScaledDamage`, replace the function body so spells read it too:

```cpp
if (!SpellData) return 0.f;

const ATMCharacter* TMChar = Cast<ATMCharacter>(Instigator);
float Damage = SpellData->BaseDamage;

if (TMChar)
{
    Damage *= TMChar->GetDamageMultiplier();
    if (SpellData->bScalesWithSpeed)
    {
        Damage *= TMChar->GetSpeedScalingFactor();
    }
}
return Damage;
```

**Test:** Place one enemy in the map. Shooting it should lower its health in steps of 25, and a FireBlast sigil should hurt it when triggered nearby. Use `BP_BaseEnemy` with the Mannequin, or even the empty capsule, since the trace only needs the capsule.

## Phase 3: Enemy lifecycle, pods and pool

Give enemies a clean spawn, chase, death and return cycle that a pod can follow. This cycle is what later keeps corpses and severed limbs around, so it needs to be reliable first.

### 1. Enemy spawn, chase and death

Add this above the class in `BaseEnemy.h`:

```cpp
class ABaseEnemy;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDied, ABaseEnemy*, Enemy);
```

Add these members to the class:

```cpp
// Public
UPROPERTY(BlueprintAssignable, Category = "Enemy|Combat")
FOnEnemyDied OnEnemyDied;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
float CorpseLifetime = 1.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
float AttackRange = 150.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
float AttackDamage = 10.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Config")
float AttackCooldown = 1.5f;

UFUNCTION(BlueprintCallable, Category = "Enemy|Status")
void ApplySlow(float SpeedMultiplier, float Duration);

UFUNCTION(BlueprintCallable, Category = "Enemy|Status")
void ApplyStun(float Duration);

// Protected
void UpdateChase();
void FinishDeath();
void EndSlow();
void EndStun();

FTimerHandle ChaseTimerHandle;
FTimerHandle DeathTimerHandle;
FTimerHandle SlowTimerHandle;
FTimerHandle StunTimerHandle;
float BaseMaxWalkSpeed = 600.f;
float LastAttackTime = -1000.f;
```

In `BaseEnemy.cpp`, include `AIController.h` and `Components/CapsuleComponent.h`. Add this to the constructor:

```cpp
AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
bUseControllerRotationYaw = false;
GetCharacterMovement()->bOrientRotationToMovement = true;
```

Add this to `BeginPlay`:

```cpp
BaseMaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
if (!OwningPool)
{
    // Enemies placed directly in the map start chasing right away.
    GetWorldTimerManager().SetTimer(ChaseTimerHandle, this, &ABaseEnemy::UpdateChase, 0.5f, true);
}
```

Then replace the lifecycle functions:

```cpp
void ABaseEnemy::OnSpawnFromPool()
{
    bIsAlive = true;
    Health = MaxHealth;
    CurrentState = EEnemyState::Idle;
    AlertnessLevel = 1.0f;

    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    SetActorTickEnabled(true);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    UCharacterMovementComponent* CMC = GetCharacterMovement();
    CMC->SetComponentTickEnabled(true);
    CMC->MaxWalkSpeed = BaseMaxWalkSpeed;
    CMC->SetMovementMode(MOVE_Walking);

    GetWorldTimerManager().SetTimer(ChaseTimerHandle, this, &ABaseEnemy::UpdateChase, 0.5f, true);
    OnSpawnEffects();
}

void ABaseEnemy::OnReturnToPool()
{
    bIsAlive = false;
    CurrentState = EEnemyState::Dead;
    GetWorldTimerManager().ClearAllTimersForObject(this);

    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);
    SetActorTickEnabled(false);

    UCharacterMovementComponent* CMC = GetCharacterMovement();
    CMC->StopMovementImmediately();
    CMC->SetComponentTickEnabled(false);

    OnReturnEffects();
}

void ABaseEnemy::Die()
{
    if (!bIsAlive) return;

    bIsAlive = false;
    CurrentState = EEnemyState::Dead;
    GetWorldTimerManager().ClearTimer(ChaseTimerHandle);
    GetWorldTimerManager().ClearTimer(SlowTimerHandle);
    GetWorldTimerManager().ClearTimer(StunTimerHandle);

    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->StopMovement();
    }

    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    OnDeathEffects();
    OnEnemyDied.Broadcast(this);

    GetWorldTimerManager().SetTimer(DeathTimerHandle, this, &ABaseEnemy::FinishDeath, CorpseLifetime, false);
}

void ABaseEnemy::FinishDeath()
{
    if (OwningPool)
    {
        OwningPool->ReturnEnemy(this);
    }
    else
    {
        Destroy();
    }
}

void ABaseEnemy::UpdateChase()
{
    if (!bIsAlive || CurrentState == EEnemyState::Stunned) return;

    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    AAIController* AI = Cast<AAIController>(GetController());
    if (!Player || !AI) return;

    const float Distance = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
    if (Distance > AwarenessRange)
    {
        AI->StopMovement();
        return;
    }

    CurrentState = EEnemyState::Combat;
    AI->MoveToActor(Player, AttackRange * 0.75f);

    const float Now = GetWorld()->GetTimeSeconds();
    if (Distance <= AttackRange && Now - LastAttackTime >= AttackCooldown)
    {
        LastAttackTime = Now;
        UGameplayStatics::ApplyDamage(Player, AttackDamage, AI, this, UDamageType::StaticClass());
    }
}

void ABaseEnemy::ApplySlow(float SpeedMultiplier, float Duration)
{
    if (!bIsAlive) return;
    GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed * SpeedMultiplier;
    GetWorldTimerManager().SetTimer(SlowTimerHandle, this, &ABaseEnemy::EndSlow, Duration, false);
}

void ABaseEnemy::EndSlow()
{
    GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed;
}

void ABaseEnemy::ApplyStun(float Duration)
{
    if (!bIsAlive) return;
    CurrentState = EEnemyState::Stunned;
    GetCharacterMovement()->DisableMovement();
    GetWorldTimerManager().SetTimer(StunTimerHandle, this, &ABaseEnemy::EndStun, Duration, false);
}

void ABaseEnemy::EndStun()
{
    if (!bIsAlive) return;
    CurrentState = EEnemyState::Combat;
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}
```

The chase is deliberately simple. It exists to test damage and later give limbs something to interrupt, and it can be replaced by proper AI later.

### 2. Let the player take damage

In `TMCharacter.h`, add this to the public section:

```cpp
virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser) override;
```

In `TMCharacter.cpp`, add the function. Ignoring the player's own damage stops FireBlast from hurting its caster:

```cpp
float ATMCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (DamageCauser == this) return 0.f;

    const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    Health = FMath::Max(0.f, Health - Applied);
    UE_LOG(LogTemp, Log, TEXT("Player health: %.0f"), Health);
    return Applied;
}
```

There's no death handling yet, so the player just stays at 0 health for now.

### 3. Start the pool and pod without Blueprint graphs

This lets you set both up with a few details-panel fields. In `EnemyObjectPool.h`, add:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Pool")
TSubclassOf<ABaseEnemy> StartupEnemyClass;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Pool")
int32 StartupPoolSize = 8;
```

Then replace `CanSpawn` so `MaxConcurrent` is enforced:

```cpp
bool CanSpawn(int32 Count = 1) const
{
    const int32 Limit = MaxConcurrent > 0 ? MaxConcurrent : PoolSize;
    return ActiveCount + Count <= Limit;
}
```

In `EnemyObjectPool.cpp`, make these changes:

```cpp
// End of BeginPlay
if (StartupEnemyClass)
{
    InitializePool(StartupEnemyClass, StartupPoolSize);
}

// Top of InitializePool, so a Blueprint call can't build it twice
if (Pool.Num() > 0) return;

// InitializePool, after hiding each parked enemy
// Use NewEnemy instead of Enemy in OnEnemyDestroyed
Enemy->SetOwningPool(this);
Enemy->OnReturnToPool();
```

This parks each enemy fully: its timers, tick and movement stay off until it's requested. It also fires **On Return Effects** once at startup, so keep that Blueprint event free of sounds for now.

Replace the loop in `PerformPoolMaintenance`. This removes the 300-second limit, which made live enemies vanish mid-fight:

```cpp
TArray<ABaseEnemy*> ToReturn;
for (const FEnemyPoolEntry& Entry : Pool)
{
    if (Entry.bIsActive && Entry.Enemy &&
        URingFrameUtils::IsBelowSurface(Entry.Enemy->GetActorLocation(), 5000.0f))
    {
        ToReturn.Add(Entry.Enemy);
    }
}
for (ABaseEnemy* Enemy : ToReturn)
{
    ReturnEnemy(Enemy);
}
```

In `EnemyPod.h`, add these two instance settings:

```cpp
UPROPERTY(EditInstanceOnly, Category = "Pod|Configuration")
TObjectPtr<AEnemyObjectPool> StartupEnemyPool;

UPROPERTY(EditInstanceOnly, Category = "Pod|Configuration")
int32 StartupPodID = 0;
```

### 4. Fix the pod

The pod constructor attaches to a root component that doesn't exist yet. Replace its two attachment lines in `EnemyPod.cpp`:

```cpp
// Replaces ActivationTrigger->SetupAttachment(RootComponent)
SetRootComponent(ActivationTrigger);

// Replaces PodVolume->SetupAttachment(RootComponent)
PodVolume->SetupAttachment(ActivationTrigger);
```

Add this at the top of `BeginPlay`, after `Super::BeginPlay()`:

```cpp
if (StartupEnemyPool)
{
    InitializePod(StartupEnemyPool, StartupPodID);
}
```

In the default spawn point loop, replace the flat offset so it also works on the ring:

```cpp
const FVector Up = URingFrameUtils::GetUpFor(this);
const FVector Tangent = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
const FVector Bitangent = FVector::CrossProduct(Up, Tangent);
const float Radians = FMath::DegreesToRadians(Angle);
const FVector Offset = (Tangent * FMath::Cos(Radians) + Bitangent * FMath::Sin(Radians)) * 500.f;
```

The enemies need to report their deaths to the pod. Add this binding next to `ActiveEnemies.Add(Enemy)` in both `SpawnPodEnemies` and `TriggerReinforcements`:

```cpp
Enemy->OnEnemyDied.AddUniqueDynamic(this, &AEnemyPod::OnEnemyDefeated);
```

Then unbind in the two places an enemy leaves the pod:

```cpp
// Top of OnEnemyDefeated, after the null check
if (!ActiveEnemies.Contains(DefeatedEnemy)) return;
DefeatedEnemy->OnEnemyDied.RemoveDynamic(this, &AEnemyPod::OnEnemyDefeated);

// DeactivatePod, before calling ReturnPodEnemies
for (ABaseEnemy* Enemy : ActiveEnemies)
{
    if (Enemy) Enemy->OnEnemyDied.RemoveDynamic(this, &AEnemyPod::OnEnemyDefeated);
}
```

In `SpawnPodEnemies`, a missing pool leaves the pod stuck in `Spawning`. Replace that early return:

```cpp
if (!EnemyPool || SpawnPoints.Num() == 0)
{
    UE_LOG(LogTemp, Warning, TEXT("EnemyPod %d: Cannot spawn - no pool or spawn points"), PodID);
    TransitionToState(EPodState::Inactive);
    return;
}
```

The escape check can't pass, because it only runs with the player inside 2,000 units yet needs them beyond 3,000. Remove the cover update and idle counter from `Tick`, leaving only `Super::Tick(DeltaTime)`, and replace `CheckEscapeConditions`:

```cpp
void AEnemyPod::CheckEscapeConditions()
{
    if (CurrentState != EPodState::Active) return;

    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;

    if (!IsPlayerInEscapeRange(Player))
    {
        TimeSinceLastEngagement = 0.f;
        bEscapeCounted = false;
        return;
    }

    if (!bEscapeCounted && bReinforcesIfPlayerEscapes)
    {
        BaseEnemyCount = FMath::Min(BaseEnemyCount + 2, MaxEnemyCount);
        bEscapeCounted = true;
    }

    TimeSinceLastEngagement += 1.f;
    if (TimeSinceLastEngagement > 30.f)
    {
        DeactivatePod();
    }
}
```

Add `bool bEscapeCounted = false;` to the protected section of `EnemyPod.h`.

Nearby pod lookup uses a collision channel no pod is on. Replace the overlap query in `GetNearbyPods`, and include `EngineUtils.h`:

```cpp
TArray<AEnemyPod*> AEnemyPod::GetNearbyPods() const
{
    TArray<AEnemyPod*> Result;

    for (TActorIterator<AEnemyPod> It(GetWorld()); It; ++It)
    {
        AEnemyPod* Pod = *It;
        if (Pod != this && Pod->CurrentState == EPodState::Active &&
            FVector::Dist(GetActorLocation(), Pod->GetActorLocation()) <= PodLinkRadius)
        {
            Result.Add(Pod);
        }
    }
    return Result;
}
```

### 5. Set it up in the map

1. Open `BP_BaseEnemy`, set the mesh to the Mannequin or leave it empty, and make sure its capsule sits on the floor. To get the Mannequin, use **Add > Add Feature or Content Pack > Third Person**.
2. Place `BP_EnemyObjectPool`, then set **Startup Enemy Class** to `BP_BaseEnemy`.
3. Place `BP_EnemyPod` about 100 cm above the floor, away from Player Start, then set **Startup Enemy Pool** to that pool.

**Test:**

- Parked enemies stay hidden and don't damage you before the pod activates.
- Walking within 2,000 units spawns 4 enemies, which chase you and log player damage.
- Killing one returns it to the pool after a second, and killing all 4 sets the pod to `Defeated`.
- Walking more than 3,000 units away deactivates the pod after about 30 seconds.

## Phase 4: Grapple

Only treat real enemies as combat targets, and make sure every grapple state can end. Right now, grappling a wall counts as a combat move, and the first combat move locks the grapple for good.

### 1. Fix input and targeting

In `TMCharacter.h`, add these to the private section:

```cpp
void OnGrappleReleased();
bool TraceFromView(FHitResult& OutHit, float Range) const;
```

In `SetupPlayerInputComponent`, replace the three `IA_Grapple` bindings. The current ones fire every frame the button is held, and again on release:

```cpp
EIC->BindAction(IA_Grapple, ETriggerEvent::Started, this, &ATMCharacter::OnGrapplePressed);
EIC->BindAction(IA_Grapple, ETriggerEvent::Completed, this, &ATMCharacter::OnGrappleReleased);
```

In `TMCharacter.cpp`, include `Transfigure.h` and `Enemy/BaseEnemy.h`, then add the helper and handlers. The helper also stops the traces from hitting the player:

```cpp
bool ATMCharacter::TraceFromView(FHitResult& OutHit, float Range) const
{
    if (!GetController()) return false;

    FVector CamLoc;
    FRotator CamRot;
    GetController()->GetPlayerViewPoint(CamLoc, CamRot);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerViewTrace), false, this);
    return GetWorld()->LineTraceSingleByChannel(
        OutHit, CamLoc, CamLoc + CamRot.Vector() * Range, ECC_Weapon, Params);
}

void ATMCharacter::OnGrapplePressed()
{
    FHitResult Hit;
    if (GrappleComponent && TraceFromView(Hit, GrappleComponent->MaxGrappleRange))
    {
        GrappleComponent->FireGrapple(Hit.ImpactPoint, Cast<ABaseEnemy>(Hit.GetActor()));
    }
}

void ATMCharacter::OnGrappleReleased()
{
    if (GrappleComponent) GrappleComponent->ReleaseGrapple();
}
```

Use the same helper in `OnGrappleKickPressed`, `OnGrappleSlamPressed`, `OnGrappleTossPressed` and `OnGrapplePullPressed`, changing the move name in each:

```cpp
void ATMCharacter::OnGrappleKickPressed()
{
    FHitResult Hit;
    if (GrappleComponent && TraceFromView(Hit, GrappleComponent->MaxGrappleRange))
    {
        GrappleComponent->ExecuteCombatMove(ECombatGrappleMove::Kick, Hit.GetActor());
    }
}
```

### 2. Make combat moves deal damage and end

In `GrappleComponent.h`, forward declare `class UCombatGrappleMove;` and `class ABaseEnemy;`, then add:

```cpp
// Public
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
TMap<ECombatGrappleMove, TSubclassOf<UCombatGrappleMove>> CombatMoveClasses;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
float CombatMoveDuration = 0.5f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
float MaxCombatPullSpeed = 2000.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple|Combat")
float EvadeSpeed = 1200.f;

// Private
FTimerHandle CombatMoveTimerHandle;
void FinishCombatMove();
void PerformEvade();
```

In `GrappleComponent.cpp`, include `Grapple/CombatGrappleMove.h` and `Enemy/BaseEnemy.h`, then replace these functions. Any move without a class in `CombatMoveClasses` uses the base move, so no Blueprint moves are needed yet.

```cpp
void UGrappleComponent::FireGrapple(FVector AimLocation, AActor* TargetEnemy)
{
    if (CurrentState != EGrappleState::Aiming && CurrentState != EGrappleState::Inactive) return;
    if (!OwnerCharacter) return;

    const bool bCombat = TargetEnemy && IsEnemyGrappleable(TargetEnemy);
    if (!OwnerCharacter->ConsumeMana(bCombat ? CombatGrappleManaCost : SwingManaCost))
    {
        return;
    }

    if (bCombat)
    {
        PerformCombatGrapple(TargetEnemy, ECombatGrappleMove::Kick);
    }
    else
    {
        PerformGrappleSwing(AimLocation);
    }
}

void UGrappleComponent::ExecuteCombatMove(ECombatGrappleMove MoveType, AActor* TargetEnemy)
{
    if (!OwnerCharacter) return;
    if (CurrentState != EGrappleState::Aiming && CurrentState != EGrappleState::Inactive) return;

    if (MoveType == ECombatGrappleMove::Evade)
    {
        PerformEvade();
        return;
    }

    // WebTrap has no target-free behavior yet.
    if (MoveType == ECombatGrappleMove::WebTrap) return;

    ABaseEnemy* Enemy = Cast<ABaseEnemy>(TargetEnemy);
    if (!Enemy || !Enemy->IsAlive() || !IsEnemyGrappleable(Enemy)) return;
    if (!OwnerCharacter->ConsumeMana(CombatGrappleManaCost)) return;

    PerformCombatGrapple(Enemy, MoveType);
}

void UGrappleComponent::PerformCombatGrapple(AActor* TargetEnemy, ECombatGrappleMove MoveType)
{
    ABaseEnemy* Enemy = Cast<ABaseEnemy>(TargetEnemy);
    if (!OwnerCharacter || !Enemy) return;

    const FVector ToEnemy = Enemy->GetActorLocation() - OwnerCharacter->GetActorLocation();
    const float PullSpeed = FMath::Min(ToEnemy.Size() * 2.f, MaxCombatPullSpeed);
    OwnerCharacter->GetCharacterMovement()->AddImpulse(ToEnemy.GetSafeNormal() * PullSpeed, true);

    TSubclassOf<UCombatGrappleMove> MoveClass = UCombatGrappleMove::StaticClass();
    if (const TSubclassOf<UCombatGrappleMove>* Found = CombatMoveClasses.Find(MoveType))
    {
        if (*Found) MoveClass = *Found;
    }

    UCombatGrappleMove* Move = NewObject<UCombatGrappleMove>(this, MoveClass);
    Move->MoveType = MoveType;
    Move->ExecuteMove(OwnerCharacter, Enemy, CurrentElement);

    CurrentState = EGrappleState::CombatGrapple;
    OnGrappleStateChanged.Broadcast(CurrentState);
    OnCombatGrappleExecuted.Broadcast(MoveType);

    FGrappleLineData CombatLine;
    CombatLine.StartPoint = OwnerCharacter->GetActorLocation();
    CombatLine.EndPoint = Enemy->GetActorLocation();
    CombatLine.Element = CurrentElement;
    CombatLine.Lifetime = CombatMoveDuration;
    CombatLine.bIsActive = true;
    ActiveLines.Add(CombatLine);
    OnGrappleLineCreated.Broadcast(CombatLine);

    GetWorld()->GetTimerManager().SetTimer(
        CombatMoveTimerHandle, this, &UGrappleComponent::FinishCombatMove, CombatMoveDuration, false);
}

void UGrappleComponent::FinishCombatMove()
{
    CurrentState = EGrappleState::Inactive;
    OnGrappleStateChanged.Broadcast(CurrentState);
}

void UGrappleComponent::PerformEvade()
{
    if (!OwnerCharacter->ConsumeMana(CombatGrappleManaCost)) return;
    OwnerCharacter->GetCharacterMovement()->AddImpulse(EvadeDirection * EvadeSpeed, true);
    OnCombatGrappleExecuted.Broadcast(ECombatGrappleMove::Evade);
}
```

`UCombatGrappleMove::ExecuteMove` then needs its damage fixes:

- Replace the Ice case's speed and timer code with `Target->ApplySlow(0.4f, 2.f);`.
- Replace the Lightning case's stun code with `Target->ApplyStun(0.75f);`, keeping the chain damage below it.
- Replace the Earth case's direction with this, which also works on the ring:

```cpp
const FVector KnockDir = (-Target->GetActorForwardVector() + Target->GetRingUp() * 0.5f).GetSafeNormal();
```

Earth still uses `CMC` for its impulse, so keep that variable.

### 3. Fix the swing

The swing moves the actor by hand while `CharacterMovement` can still move it too. Pause `CharacterMovement` for the swing, and let the swing own the velocity.

Add this in `PerformGrappleSwing`, just before `bIsSwinging = true`:

```cpp
OwnerCharacter->GetCharacterMovement()->SetMovementMode(MOVE_None);
```

In `UpdateSwingPhysics`, remove the `CMC->GravityScale = 0.f;` line, then replace step 6:

```cpp
FHitResult SweepHit;
OwnerCharacter->SetActorLocation(NewPos, true, &SweepHit);
if (SweepHit.bBlockingHit)
{
    SwingVelocity = FVector::VectorPlaneProject(SwingVelocity, SweepHit.ImpactNormal);
}
CMC->Velocity = SwingVelocity;
```

In `ReleaseGrapple`, replace the block that restores gravity and velocity:

```cpp
if (OwnerCharacter)
{
    auto* CMC = OwnerCharacter->GetCharacterMovement();
    CMC->SetMovementMode(MOVE_Falling);
    CMC->Velocity = SwingVelocity;
}
```

**Test:**

- Aiming at the wall and holding grapple starts a swing, and releasing it carries your speed into the fall.
- You can swing again right after landing.
- Grappling an enemy pulls you in and lowers its health.
- Grappling again works about half a second after a combat move.
- Pressing Evade dashes you in your movement direction.

## Phase 5: Spells, sigils and buffs

Finish with the effects players can already reach: the Temporal Clap, combos, sigil types, healing, buffs and slows. Each fix is small and can be tested with `FireBlast` and one placeholder healing sigil.

### 1. Temporal Clap

The first sigil never fires, because a timer with a delay of 0 doesn't run. Replace the loop in `TriggerSigilsSynchronized`:

```cpp
for (ASigilActor* Sigil : PendingTemporalSigils)
{
    if (!Sigil || !Sigil->IsArmed()) continue;

    if (CurrentDelay <= 0.f)
    {
        OnSigilTemporalActivate.Broadcast(Sigil);
        Sigil->TriggerManual();
    }
    else
    {
        TWeakObjectPtr<ThisClass> WeakThis(this);
        TWeakObjectPtr<ASigilActor> WeakSigil(Sigil);

        FTimerHandle DelayHandle;
        GetWorld()->GetTimerManager().SetTimer(DelayHandle,
            FTimerDelegate::CreateLambda([WeakThis, WeakSigil]()
            {
                if (WeakThis.IsValid() && WeakSigil.IsValid() && WeakSigil->IsArmed())
                {
                    WeakThis->OnSigilTemporalActivate.Broadcast(WeakSigil.Get());
                    WeakSigil->TriggerManual();
                }
            }),
            CurrentDelay, false);
    }

    CurrentDelay += DelayBetween;
}
```

World timers also slow down during the clap, so the 3-second effect currently lasts 15 real seconds. The player is slowed too. This version assumes you want the player at normal speed in a slowed world.

In `ATMCharacter::StartTemporalEffects`, replace the time dilation lines:

```cpp
UGameplayStatics::SetGlobalTimeDilation(GetWorld(), TemporalTimeDilation);
CustomTimeDilation = 1.f / FMath::Max(TemporalTimeDilation, 0.01f);
```

In `ActivateTemporalClap`, multiply both timer lengths so they run in real seconds:

```cpp
ClapAnimationDuration * TemporalTimeDilation   // ClapAnimationTimerHandle
TemporalClapDuration * TemporalTimeDilation    // TemporalTimerHandle
```

If you'd rather slow the player along with the world, skip the `CustomTimeDilation` change. If the clap montage starts playing through `PlayClapAnimation`, set its play rate to 1, since the player's own time is no longer slowed.

### 2. Combos

`FiredCombosThisFrame` empties every tick, so a combo fires every frame while two matching sigils are close. In `TransfigurationComboDetector.h`, replace it with a set that lasts as long as the pair does:

```cpp
TSet<TTuple<TWeakObjectPtr<ASigilActor>, TWeakObjectPtr<ASigilActor>>> FiredCombos;
```

In `TickComponent`, replace `FiredCombosThisFrame.Empty();` with:

```cpp
for (auto It = FiredCombos.CreateIterator(); It; ++It)
{
    const bool bAValid = It->Key.IsValid() && It->Key->IsArmed();
    const bool bBValid = It->Value.IsValid() && It->Value->IsArmed();
    if (!bAValid || !bBValid)
    {
        It.RemoveCurrent();
    }
}
```

In `CheckForCombos`, replace the pair lookup and add:

```cpp
const TTuple<TWeakObjectPtr<ASigilActor>, TWeakObjectPtr<ASigilActor>> Pair(A, B);
if (FiredCombos.Contains(Pair)) continue;

// Inside the recipe match
FiredCombos.Add(Pair);
```

Then delete the `// Update chain detection` loop at the bottom of `CheckForCombos`. It counts the same sigils again every frame, and nothing calls `StartComboChain` yet.

### 3. Sigil types

Placement always spawns a plain `ASigilActor`, so a spell can't become a healing sigil or use a Blueprint subclass. Add this to `TransfigurationDefinition.h`, with a forward declaration of `class ASigilActor;`:

```cpp
/** Optional override. Empty uses the native class that fits this spell. */
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell|Sigil")
TSubclassOf<ASigilActor> SigilActorClass;
```

In `ConfirmPlacement`, replace the spawn call:

```cpp
TSubclassOf<ASigilActor> SigilClass = SpellDef->SigilActorClass;
if (!SigilClass)
{
    SigilClass = SpellDef->bIsPersistentSigil
        ? TSubclassOf<ASigilActor>(APersistentSigilActor::StaticClass())
        : TSubclassOf<ASigilActor>(ASigilActor::StaticClass());
}

ASigilActor* Sigil = GetWorld()->SpawnActor<ASigilActor>(SigilClass, SpawnLoc, SigilRot, SpawnParams);
```

### 4. Healing and buffs

Healing sigils skip their caster, which is the only player. In `PersistentSigilActor.cpp:71`, change the check to:

```cpp
if (TMChar)
```

The speed buff can't find a `UBuffEffectComponent` on the player, so add one. In `TMCharacter.h`, forward declare `class UBuffEffectComponent;` if it isn't already there, then add:

```cpp
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Magic")
TObjectPtr<UBuffEffectComponent> BuffEffects;

float GetSpeedBuffMultiplier() const;
```

In `TMCharacter.cpp`:

```cpp
// Constructor
BuffEffects = CreateDefaultSubobject<UBuffEffectComponent>(TEXT("BuffEffects"));

// New function
float ATMCharacter::GetSpeedBuffMultiplier() const
{
    return BuffEffects ? BuffEffects->GetBuffMultiplier(EBuffType::Speed) : 1.f;
}
```

Movement states also overwrite `MaxWalkSpeed` when they start, which would wipe the buff. Change those lines:

```cpp
// TMState_Idle::Enter and TMState_Walk::Enter
Owner->GetCharacterMovement()->MaxWalkSpeed = Owner->WalkSpeed * Owner->GetSpeedBuffMultiplier();

// TMState_Sprint::Enter
Owner->GetCharacterMovement()->MaxWalkSpeed = Owner->SprintSpeed * Owner->GetSpeedBuffMultiplier();
```

### 5. Slows that can't stack forever

Each ice hit saves the already slowed speed and restores it later, so a six-point ice line left enemies at 18.75 speed permanently. Use the Phase 3 functions everywhere instead:

| File | Replace | With |
| --- | --- | --- |
| `IceWallEffect.cpp`, `SpawnIceTrail` | The speed and timer block | `Enemy->ApplySlow(0.4f, 3.f);` |
| `GrappleLine.cpp`, `ApplyIceEffect` | The speed and timer block | `Enemy->ApplySlow(0.5f, 2.f);` |
| `GrappleLine.cpp`, `ApplyLightningEffect` | The stun and timer block | `Enemy->ApplyStun(0.75f);` |

**Test:**

- With 2 sigils placed, the clap triggers both, and slow motion lasts about 3 real seconds.
- Two matching sigils placed close together fire one combo event once you add a recipe to `ComboRecipes`.
- A test spell with **Is Persistent Sigil** and **Is Healing Sigil** on heals you while you stand in it.
- A test spell with **Is Persistent Sigil** and **Is Buff Sigil** on keeps you faster through walking and sprinting, then ends after 3 seconds.
- Temporarily setting `CurrentElement` to `Ice` in `GrappleComponent.h` makes a combat grapple slow an enemy for 2 seconds, then its speed returns to normal.

`OnDrawSigilPressed` only draws `FireBlast`, so temporarily change that name to cast each test spell. Add those spells to `DA_SpellRegistry` too.

## Fixes to leave for later

These can wait because they don't block the limb system or a flat test map. Some are unfinished features rather than quick bugs, so rebuilding them now would mostly be wasted work.

| Item | Current problem | When to fix it |
| --- | --- | --- |
| Environmental destruction | Requests pass a null target and get dropped. The async task also holds a reference to a local variable, and finished tasks are never queued. | Rebuild when destructible levels exist. Keep `ChunkPoolSize = 0` until then. |
| Portals | Nothing links them. Teleports can bounce straight back, then block the actor for good. The exit uses the wrong portal's direction (`PortalSigilActor.cpp:122`). | When you add a portal spell. |
| Grapple line actors | Nothing spawns `AGrappleLine` or `AElementalGrappleEffect`, so elemental trails never run. | When grapple visuals are ready. |
| Web lines and Web Trap | `FindWebConnectionPoints` returns nothing, and Web Trap has no behavior. | When you design the web tier. |
| Combo chains | Nothing calls `StartComboChain`. | When chain spells are designed. |
| Mana refunds | The clap's refund can't be reached, and nothing calls `NotifySpellHitResult`. | When you balance spells. |
| Clap camera tilt | It changes the control rotation, which the player's camera no longer uses. | When you polish the clap. |
| Ring world mode | The ring band has no collision, and these still assume world Z is up: `GrappleLine.cpp:329`, `IceWallEffect.cpp:59` and `TemporalClapAnimInstance.cpp:55`. | Before turning `bFlatWorldForTesting` off. |
| Player death and proper enemy AI | The player stops at 0 health, and enemies only chase and hit. | After the limb system proves out. |

## Ready for the limb system

Start the limb system once this list passes on `L_TestArena`. Commit after each phase, so a bad change is easy to undo.

- [ ] Phase 0: The game opens the test map, the player stays on the floor, and sigils can be drawn.
- [ ] Phase 1: Turning, sprint, ledge drops, double jump and wall run all behave.
- [ ] Phase 2: Spells and shots lower enemy health.
- [ ] Phase 3: Pod enemies spawn, chase, die and return to the pool.
- [ ] Phase 4: Swings and combat grapples can repeat without locking up.
- [ ] Phase 5: The clap, combos, healing, buffs and slows each work once and end cleanly.

The limb system's first slice will build on these parts:

| Limb system need | Comes from |
| --- | --- |
| One place to find which body part was hit | `TakePoolDamage`, Phase 2 |
| Exact shots against body parts | `ECC_Weapon` trace, Phase 2 |
| Corpses that stay around | `CorpseLifetime`, `FinishDeath`, Phase 3 |
| A full body each time an enemy respawns | `OnSpawnFromPool`, Phase 3 |
| Severed parts that survive their owner | Pooled enemies with clean timers, Phase 3 |
| Correct gravity for severed parts | `URingFrameUtils::GetDownFor`, Phase 0 |
| Free animation | A hidden Mannequin skeleton, Phase 3 |

The first slice will generate one placeholder zombie from math, attach its parts to the Mannequin's bones, and let you shoot limbs off on this map. No modeling is required.
