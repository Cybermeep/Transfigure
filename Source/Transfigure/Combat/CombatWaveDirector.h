// Source/Transfigure/Combat/CombatWaveDirector.h
//
// Endless, escalating enemy waves for an arena (COMBAT_LOOP_PLAN C1, A3).
// Place one in a map (Tools/make_combat_arena.py does). It spawns enemies at
// actors tagged EnemySpawn (away from the player, out of view when it can),
// keeps a few alive at once, gives a breather between waves, keeps score, and
// stops when the player dies. The HUD reads it for the run bar and death screen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatWaveDirector.generated.h"

class ABaseEnemy;

UENUM(BlueprintType)
enum class ERunPhase : uint8
{
    Starting,   // countdown before wave 1
    Fighting,
    Breather,   // between waves
    Over        // the player died
};

UCLASS()
class TRANSFIGURE_API ACombatWaveDirector : public AActor
{
    GENERATED_BODY()

public:
    ACombatWaveDirector();

    /** The director in this world, if there is one. */
    static ACombatWaveDirector* Find(const UObject* WorldContext);

    /** What spawns (default: procedural zombies). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    TSubclassOf<ABaseEnemy> EnemyClass;

    /** Seconds before wave 1, and between waves. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    float StartDelay = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    float BreatherTime = 6.f;

    /** Enemies in wave 1; each later wave adds ExtraPerWave, up to MaxPerWave. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    int32 FirstWaveCount = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    int32 ExtraPerWave = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    int32 MaxPerWave = 30;

    /** How many can be alive at once in wave 1; one more each wave, up to MaxAliveCap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    int32 FirstMaxAlive = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    int32 MaxAliveCap = 12;

    /** Seconds between spawns in wave 1; 8% shorter each wave, never below MinSpawnInterval. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    float SpawnInterval = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    float MinSpawnInterval = 0.4f;

    /** Enemy health grows by this much each wave (0.1 = +10%). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    float HealthGrowthPerWave = 0.1f;

    /** Never spawn closer than this to the player. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    float MinSpawnDistance = 1800.f;

    /** Actors with this tag are spawn points. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
    FName SpawnPointTag = TEXT("EnemySpawn");

    ERunPhase GetPhase() const { return Phase; }
    int32 GetWave() const { return Wave; }
    /** Still to come this wave, plus alive. */
    int32 GetEnemiesLeft() const { return ToSpawn + Alive.Num(); }
    int32 GetKills() const { return Kills; }
    int32 GetScore() const { return Score; }
    /** Best score since the editor / game started (kept across restarts). */
    int32 GetBestScore() const;
    bool IsNewBest() const { return bNewBest; }
    /** Seconds of fighting so far (the countdown before wave 1 doesn't count). */
    float GetRunTime() const { return RunTime; }
    /** Seconds until the next wave (while Starting / Breather). */
    float GetPhaseTimeLeft() const { return PhaseTimeLeft; }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    void StartWave();
    void EndRun();
    bool SpawnEnemy();
    bool PickSpawnPoint(FVector& Out) const;

    UFUNCTION()
    void HandleEnemyDied(ABaseEnemy* Enemy);

    ERunPhase Phase = ERunPhase::Starting;
    int32 Wave = 0;
    int32 ToSpawn = 0;
    int32 Kills = 0;
    int32 Score = 0;
    bool bNewBest = false;
    float RunTime = 0.f;
    float WaveTime = 0.f;
    float PhaseTimeLeft = 0.f;
    float NextSpawnIn = 0.f;

    TArray<FVector> SpawnPoints;
    TArray<TWeakObjectPtr<ABaseEnemy>> Alive;

    /** Stuck check: where each enemy was last seen moving, and for how long it hasn't. */
    struct FStuckTrack
    {
        FVector LastPos = FVector::ZeroVector;
        float StillTime = 0.f;
    };
    TMap<TWeakObjectPtr<ABaseEnemy>, FStuckTrack> Stuck;
    void UnstickEnemies(float DeltaSeconds);
};
