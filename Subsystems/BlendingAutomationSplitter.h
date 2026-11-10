#pragma once

#include "CoreMinimal.h"
#include "BlendingAutomationTypes.h"

class FBlendingAutomationSplitter
{
public:
    static bool PlaceMarkersAndSplit(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        const FString& OriginalAnimationSrtPath,
        UAnimSequence* originalAnimSequence,
        FGuid SkeletalMeshBindingId,
        int32 SubIndex,
        const FString& Label,
        TMap<int32, FSectionLabelEntry>& OutMarkerSectionMap);
 
private:
    static bool PlaceMarkersFromVTT(
        const FString& OriginalAnimationSrtPath,
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TArray<FMovieSceneMarkedFrame>& PlacedMarkers
    );

    static bool SplitAnimationSections(
        UMovieSceneSkeletalAnimationSection* Section,
        UMovieScene* MovieScene,
        const TArray<FMovieSceneMarkedFrame>& Markers,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap);

    static bool SplitAnimationSection(
        UMovieSceneSkeletalAnimationSection* Section,
        UMovieScene* MovieScene,
        const TArray<FMovieSceneMarkedFrame>& Markers,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        int32 SubIndex,
        const FString& Label);
};