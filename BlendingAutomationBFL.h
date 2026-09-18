#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSequence.h"
#include "MovieSceneSection.h"
#include "MovieScene.h"
#include "Animation/AnimSequence.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "SequencerAbstractionBPLibrary.h"
#include "BlendingAutomationBFL.generated.h"

UCLASS()
class BLENDINGAUTOMATIONEDITOR_API UBlendingAutomationBFL : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Blending Automation")
    static bool ProcessAnimationSubstitution(
        ULevelSequence* LevelSequence,
        const FString& OriginalAnimationPath,
        const FString& OriginalAnimationSrtPath,
        const FString& DonorAnimationPath,
        const FString& Label,
        int32 SubIndex,
        UAnimSequence*& OutNewAnimation);

private:
    static bool ValidateInputParameters(
        ULevelSequence* LevelSequence,
        const FString& OriginalAnimationPath,
        const FString& OriginalAnimationSrtPath,
        const FString& DonorAnimationPath,
        const FString& Label,
        int32 SubIndex);

    static bool FindAllTracks(
        UMovieScene* MovieScene,
        TArray<UMovieSceneSkeletalAnimationTrack*>& OutTracks);

    static bool FindAllSections(
        UMovieScene* MovieScene,
        TArray<UMovieSceneSkeletalAnimationSection*>& OutSections);

    static bool SplitAnimationSections(
        UMovieSceneSkeletalAnimationSection* Section,
        UMovieScene* MovieScene,
        const TArray<FMovieSceneMarkedFrame>& Markers,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap);

};