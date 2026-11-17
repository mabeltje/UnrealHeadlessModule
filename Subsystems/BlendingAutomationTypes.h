/**
 * BlendingAutomationTypes.h
 * 
 * Defines the data structures used for blending pipeline 
 * for the substitution request and the context passed 
 * through the stages of the pipeline.
 */
#pragma once

#include "CoreMinimal.h"
#include "MovieScene.h"
#include "LevelSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "SequencerAbstractionBPLibrary.h"
#include "MocapImportSubsystem.h"
#include "BlendingAutomationTypes.generated.h"

// TODO: in file SkeletalSequenceDataAsset.h? with blending offsets and hardcoded names and filepaths.
USTRUCT(BlueprintType)
struct FSubstitutionRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<ULevelSequence> LevelSequence = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString OriginalAnimationPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString OriginalAnimationSrtPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DonorAnimationPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString OutputAnimationPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 SubIndex = 0;
};

// Context passed internally through pipeline stages
struct FSubstitutionContext
{
    USkeleton* TargetSkeleton = nullptr;
    USkeletalMesh* TargetSkeletalMesh = nullptr;
    UMovieScene* MovieScene = nullptr;
    
    FFrameRate DisplayRate;
    FFrameRate TickResolution;

    UAnimSequence* OriginalAnim = nullptr;
    UAnimSequence* DonorAnim = nullptr;
    
    UMovieSceneSkeletalAnimationTrack* Track = nullptr;
    FGuid SkeletalMeshBindingId;

    UMocapImportSubsystem* MocapSubsystem = nullptr;
    
    TMap<int32, FSectionLabelEntry> MarkerSectionMap;

};