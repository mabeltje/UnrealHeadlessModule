#pragma once

#include "CoreMinimal.h"
#include "BlendingAutomationTypes.h"

/**
 * BlendingAutomationExporter.cpp
 * 
 * Bakes the blended animation sequences to a new animation sequence.
 * Work in progress: Exporting the markers to an SRT file does not work yet.
 */

class FBlendingAutomationExporter
{
public:
    /** Bakes a new animation sequence from the given level sequence and binding.
     * 
     * @param LevelSequence The level sequence containing the animation data.
     * @param MovieScene The movie scene associated with the level sequence.    
     * @param SkeletalMeshBindingId The binding ID for the skeletal mesh track.
     * @param Label The label used to name the new baked animation sequence.
     */
    static bool BakeAnimationSequence(
        FSubstitutionContext& Context, 
        const FSubstitutionRequest& Request
    );
 
private:
    /** Exports all marked frames in the Level Sequence to an SRT subtitle file.
     *
     * @param LevelSequence The Level Sequence object containing the markers.
     * @param OutputSrtFilePath The full target output path for the SRT file.
     * @return True if the SRT file was exported successfully.
     */
    static bool ExportMarkersToSrt(
        const FSubstitutionContext& Context,
        const FString& OutputSrtFilePath
    );

};