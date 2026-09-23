#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VTTParser.h" // Needed by value for TArray<FVTTEntry>
#include "BlendingAutomationBFL.generated.h"

// Forward declarations instead of #includes
class ULevelSequence;
class UMovieScene;
class USkeleton;
class UAnimSequence;
class UMocapImportSubsystem;
class UMovieSceneSkeletalAnimationTrack;
class UMovieSceneSkeletalAnimationSection;
struct FMovieSceneMarkedFrame;
struct FSectionLabelEntry;

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

    static bool LoadAnimSequence(
        UMocapImportSubsystem* MocapSubsystem,
        const FString& AnimationPath,
        USkeleton* TargetSkeleton,
        UAnimSequence*& OutAnimSequence);

    static bool PlaceMarkersFromVTT(
        const FString& OriginalAnimationSrtPath,
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TArray<FMovieSceneMarkedFrame>& PlacedMarkers
    );

    static bool RemoveSectionFromLevelSequence(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        int32 SubIndex,
        TMap<int32, FSectionLabelEntry> MarkerSectionMap,
        FFrameRate DisplayRate
    );

    static bool AddDonorAnimationSection(
        UMovieSceneSkeletalAnimationTrack* Track, 
        UAnimSequence* donorAnimSequence
    );

    static void PrintSections(UMovieScene* MovieScene);

};