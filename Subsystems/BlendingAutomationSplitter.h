/**
 * BlendingAutomationSplitter.h
 *
 * Stage 2 of the blending automation pipeline: parses SRT/VTT markers into the Level Sequence,
 * inserts the base animation track, and splits it into Head, Target, and Tail sections.
 */

#pragma once

#include "CoreMinimal.h"
#include "BlendingAutomationTypes.h"

class FBlendingAutomationSplitter
{
public:
    /**
     * Executes Stage 2 of the pipeline: populates timeline markers from the SRT/VTT file,
     * adds the original animation to the skeletal binding, and splits it around SubIndex.
     *
     * @param Context  Mutable pipeline context receiving the split MarkerSectionMap.
     * @param Request  Immutable substitution request containing the SRT path and SubIndex.
     * @return         True if markers were placed and the animation section was split cleanly.
     */
    static bool PlaceMarkersAndSplit(
        FSubstitutionContext& SubstitutionContext,
        const FSubstitutionRequest& SubstitutionRequest);
 
private:
    // Parses the SRT/VTT file and places markers into the Level Sequence.
    static bool PlaceMarkersFromVTT(
        FSubstitutionContext& SubstitutionContext,
        const FSubstitutionRequest& SubstitutionRequest);

    // TODO: are both of these function necessary?
    // Splits the animation section into segments based on the places markers
    static bool SplitAnimationSections(
        UMovieSceneSkeletalAnimationSection* Section,
        const TArray<FMovieSceneMarkedFrame>& RelevantMarkers,
        FSubstitutionContext& SubstitutionContext,
        const FSubstitutionRequest& SubstitutionRequest);

    // Splits the animation section into segments based on the places markers, but only around the SubIndex
    static bool SplitAnimationSection(
        UMovieSceneSkeletalAnimationSection* Section,
        FSubstitutionContext& SubstitutionContext,
        const FSubstitutionRequest& SubstitutionRequest);
};