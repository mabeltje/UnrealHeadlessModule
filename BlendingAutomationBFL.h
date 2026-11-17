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

DECLARE_LOG_CATEGORY_EXTERN(LogBlendingAuto, Log, All);

UCLASS()
class BLENDINGAUTOMATIONEDITOR_API UBlendingAutomationBFL : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** */
    UFUNCTION(BlueprintCallable, Category = "Blending Automation")
    static bool ProcessAnimationSubstitution(
        ULevelSequence* LevelSequence,
        const FString& OriginalAnimationPath,
        const FString& OriginalAnimationSrtPath,
        const FString& DonorAnimationPath,
        const FString& Label,
        const FString& OutputAnimationPath,
        int32 SubIndex,
        UAnimSequence*& OutNewAnimation);


private:
    
    static bool LoadAndValidate(
        ULevelSequence* LevelSequence,
        const FString& OriginalAnimationPath,
        const FString& OriginalAnimationSrtPath,
        const FString& DonorAnimationPath,
        const FString& Label,
        int32 SubIndex,
        USkeleton*& OutSkeleton,
        USkeletalMesh*& OutMesh,
        UMovieScene*& OutMovieScene,
        FFrameRate& OutDisplayRate,
        FFrameRate& OutTickResolution,
        UAnimSequence*& OutOriginalAnim,
        UAnimSequence*& OutDonorAnim,
        UMocapImportSubsystem*& OutMocapSubsystem,
        UMovieSceneSkeletalAnimationTrack*& OutTrack,
        FGuid& OutSkeletalMeshBindingId);

    static bool ReplaceSection(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        UMovieSceneSkeletalAnimationTrack* Track,
        UAnimSequence* DonorAnim,
        int32 SubIndex,
        const FString& Label);


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


    static bool LoadAnimSequence(
        UMocapImportSubsystem* MocapSubsystem,
        const FString& AnimationPath,
        USkeleton* TargetSkeleton,
        UAnimSequence*& OutAnimSequence);


    static bool RemoveSectionFromLevelSequence(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        int32 SubIndex,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        FFrameRate DisplayRate
    );

    static bool AddDonorAnimationSection(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        UMovieSceneSkeletalAnimationTrack* Track, 
        UAnimSequence* donorAnimSequence,
        int32 SectionIndex,
        const FString& Label
    );

    static bool ResetLevelSequence(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FGuid SkeletalMeshBindingId
    );


    static bool FitTailSections(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        int32 SectionIndex,
        UAnimSequence* donorAnimSequence
    );

    static void PrintSections(UMovieScene* MovieScene);

};