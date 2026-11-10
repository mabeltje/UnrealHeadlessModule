#pragma once

#include "CoreMinimal.h"
#include "BlendingAutomationTypes.h"

class FBlendingAutomationBlender
{
public:
    static bool BlendAnimationSectionsAndMatchToBone(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        int32 SectionIndex,
        USkeletalMesh* TargetSkeletalMesh);
 
private:
    static bool BlendAnimationSections(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        int32 SectionIndex
    );

    static bool MatchSectionsToBone(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        int32 SectionIndex,
        USkeletalMesh* TargetSkeletalMesh
    );

};