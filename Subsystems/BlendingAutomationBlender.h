/**
 * BlendingAutomationBlender.h
 *
 * Stage 4 of the blending automation pipeline: positions adjacent animation sections
 * for blending, aligns pelvis bone transforms across sections, and updates the sequence playback range.
 */

 #pragma once

#include "CoreMinimal.h"
#include "BlendingAutomationTypes.h"

class FBlendingAutomationBlender
{
public:
    /**
     * Executes Stage 4 of the pipeline: spaces sections for transitions, aligns pelvis
     * transforms across section boundaries, and updates the Level Sequence playback range.
     *
     * @param SubstitutionContext  Mutable pipeline context containing the MovieScene, rates, and section map.
     * @param SubstitutionRequest  Immutable substitution request containing the target Level Sequence.
     * @return         True if section blending and bone matching succeeded.
     */
    static bool BlendAnimationSectionsAndMatchToBone(
        FSubstitutionContext& SubstitutionContext, 
        const FSubstitutionRequest& SubstitutionRequest);
 
private:
    static bool BlendAnimationSections(
        FSubstitutionContext& SubstitutionContext, 
        const FSubstitutionRequest& SubstitutionRequest,
        int32 SectionIndex
    );

    static bool MatchSectionsToBone(
        FSubstitutionContext& SubstitutionContext,
        const FSubstitutionRequest& SubstitutionRequest,
        int32 SectionIndex
    );

    static bool BakeBindingToControlRig(
        FSubstitutionContext& SubstitutionContext,
        const FSubstitutionRequest& SubstitutionRequest
    );

    static void PrepareTracksForSubstitution(UMovieScene* MovieScene, const FGuid& BindingId);

};