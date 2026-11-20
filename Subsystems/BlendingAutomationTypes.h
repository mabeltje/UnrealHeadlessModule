/**
 * BlendingAutomationTypes.h
 *
 * Defines the core data structures passed through the blending automation pipeline:
 *   - FSubstitutionRequest: Immutable input parameters provided by the caller/Blueprint.
 *   - FSubstitutionContext: Mutable runtime state shared across pipeline stages.
 */

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"

#include "MocapImportSubsystem.h"
#include "SequencerAbstractionBPLibrary.h"
#include "BlendingAutomationTypes.generated.h"

// TODO: in file SkeletalSequenceDataAsset.h? with blending offsets and hardcoded names and filepaths.
USTRUCT(BlueprintType)
struct FSubstitutionRequest
{
    GENERATED_BODY()

    /** Target Level Sequence in which the animation substitution will be performed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending Automation")
    TObjectPtr<ULevelSequence> LevelSequence = nullptr;

    /** File path to the base FBX animation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending Automation")
    FString OriginalAnimationPath;

    /** File path to the SRT/VTT marker file defining segment boundaries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending Automation")
    FString OriginalAnimationSrtPath;

    /** File path to the replacement (donor) FBX animation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending Automation")
    FString DonorAnimationPath;

    /** Destination directory/base path for the exported FBX. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending Automation")
    FString OutputAnimationPath;

    /** Identifier label applied to the baked asset and exported FBX. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending Automation")
    FString Label;

    /** Zero-based gloss index specifying which segment to replace. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending Automation")
    int32 SubIndex = 0;
};



/** Internal runtime context struct populated in Stage 1 and passed across Stages 2–5. */
struct FSubstitutionContext
{
    // --- Target Rig & Sequencer References ---
    USkeleton* TargetSkeleton = nullptr;
    USkeletalMesh* TargetSkeletalMesh = nullptr;
    UMovieScene* MovieScene = nullptr;
    UMovieSceneSkeletalAnimationTrack* Track = nullptr;
    FGuid SkeletalMeshBindingId;

    // --- Sequence Timing ---
    FFrameRate DisplayRate;
    FFrameRate TickResolution;

    // --- Imported Animation Assets ---
    UAnimSequence* OriginalAnim = nullptr;
    UAnimSequence* DonorAnim = nullptr;

    // --- Editor Subsystems & Timeline Mapping ---
    UMocapImportSubsystem* MocapSubsystem = nullptr;
    TMap<int32, FSectionLabelEntry> MarkerSectionMap;
    TArray<FMovieSceneMarkedFrame> PlacedMarkers;
};