/**
 * BlendingAutomationSplitter.cpp
 *
 * Implements Stage 2 of the blending automation pipeline:
 *   1. Parse start/end gloss markers from the SRT/VTT file and place them on the timeline.
 *   2. Insert the original animation sequence onto the target skeletal mesh binding.
 *   3. Split the section before and after the target SubIndex into [Head, Target, Tail] segments.
 */

#include "BlendingAutomationSplitter.h"

#include "../BlendingAutomationBFL.h"
#include "SequencerAbstractionBPLibrary.h"
#include "VTTParser.h"


bool FBlendingAutomationSplitter::PlaceMarkersAndSplit(
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest)
{
    // Parse SRT and place markers on the timeline
    TArray<FMovieSceneMarkedFrame> PlacedMarkers;
    bool bMarkersPlaced = PlaceMarkersFromVTT(SubstitutionContext, SubstitutionRequest);
    if (!bMarkersPlaced)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("No markers were placed from the VTT file."));
        return false;
    }  
    
    // Add the original animation sequence to the skeletal mesh binding
    FSequenceOpenResult ResultError;
    UMovieSceneSkeletalAnimationSection* AnimSection = USequencerAbstractionBPLibrary::AddAnimSectionToBinding(SubstitutionRequest.LevelSequence, SubstitutionContext.SkeletalMeshBindingId, SubstitutionContext.OriginalAnim, 0, 0, false, ResultError);
    
    if (AnimSection == nullptr)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to add original animation section to the level sequence."));
        return false;
    } 

    SubstitutionContext.MarkerSectionMap.Empty();

    // TODO: Also look if the transition before and after the subindex section needs to be removed
    // Cut the animation into segments based on the markers, but only split before and after the subindex
    bool bSplitSuccess = SplitAnimationSection(AnimSection, SubstitutionContext, SubstitutionRequest);
    if (!bSplitSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to split animation sections based on markers."));
        return false;
    }

    return true;
}


bool FBlendingAutomationSplitter::PlaceMarkersFromVTT(
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest) 
{   
    TArray<FVTTEntry> OutMarkers;
    bool bSuccess = UVTTParser::ParseVTTFile(SubstitutionRequest.OriginalAnimationSrtPath, OutMarkers);

    if (!bSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to parse VTT file."));
    }

    
    for (FVTTEntry& Marker : OutMarkers)
    {
        UVTTParser::UpdateVTTEntryFrames(Marker, SubstitutionContext.DisplayRate); 

        // Add markers to the level sequence
        TArray<FMovieSceneMarkedFrame> NewMarkers = UVTTParser::AddVTTEntryMarkers(SubstitutionRequest.LevelSequence, Marker.StartFrame.Value, Marker.EndFrame.Value, Marker.Text);
        SubstitutionContext.PlacedMarkers.Append(NewMarkers);
    }

    return bSuccess;
}


bool FBlendingAutomationSplitter::SplitAnimationSection(
    UMovieSceneSkeletalAnimationSection* Section, 
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest) 
{

    // Sort placed markers in the MovieScene to ensure they are in chronological order
    UMovieScene* MovieScene = SubstitutionContext.MovieScene;
    SubstitutionContext.MovieScene->SortMarkedFrames();
    
    // get markers again to make sure they are in chronological order
    TArray<FMovieSceneMarkedFrame> SortedMarkers = MovieScene->GetMarkedFrames();

    // Find the 2 markers that correspond to the s: and e: labels for the given label 
    FMovieSceneMarkedFrame StartMarker;
    FMovieSceneMarkedFrame EndMarker;
    bool bFoundStart = false;
    bool bFoundEnd = false;

    int32 GlossIndex = 0;
    int32 StartMarkerIndex = INDEX_NONE;
    int32 EndMarkerIndex = INDEX_NONE;
    
    // find out which indices are the start and end markers in the sequence
    for (int32 i = 0; i < SortedMarkers.Num(); ++i)
    {   
        const FMovieSceneMarkedFrame& Marker = SortedMarkers[i];

        if (Marker.Label.StartsWith(TEXT("s:")))
        {   
            if (GlossIndex == SubstitutionRequest.SubIndex)
            {
                StartMarker = Marker;
                StartMarkerIndex = i;
                bFoundStart = true;
            }
        }
        else if (Marker.Label.StartsWith(TEXT("e:")))
        {   
            if (GlossIndex == SubstitutionRequest.SubIndex)
            {
                EndMarker = Marker;
                EndMarkerIndex = i;
                bFoundEnd = true;
            }
            GlossIndex++; // Increment pair index on encountering an end marker
        }
    }
    
    if (!bFoundStart || !bFoundEnd)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find start or end markers for SubIndex: %d"), SubstitutionRequest.SubIndex);
        return false;
    }

    // Now determine where to cut the section
    FMovieSceneMarkedFrame CutStartMarker = StartMarker;
    FMovieSceneMarkedFrame CutEndMarker = EndMarker;

    const bool bIsFirstSubIndex = (SubstitutionRequest.SubIndex == 0);
    // The last available pair of s:/e: markers is GlossIndex - 1 (due to post-increment)
    const bool bIsLastSubIndex = (SubstitutionRequest.SubIndex == GlossIndex - 1);

    // if subindex 0
        // if end of index 0 and start of index 1 is same
            // cut at start and end of index 0
        // if end of index 0 is earlier than start of index 1 (there is a transition)
            // cut at start of index 0 and start of index 1
    if (bIsFirstSubIndex)
    {
        // Find start marker of next index (SubIndex 1) by scanning forward
        int32 NextStartIdx = INDEX_NONE;
        for (int32 i = StartMarkerIndex + 1; i < SortedMarkers.Num(); ++i)
        {
            if (SortedMarkers[i].Label.StartsWith(TEXT("s:")))
            {
                NextStartIdx = i;
                break;
            }
        }

        if (NextStartIdx != INDEX_NONE)
        {
            const FMovieSceneMarkedFrame& NextStartMarker = SortedMarkers[NextStartIdx];
            
            // If end of index 0 is earlier than start of index 1 (there is a transition gap)
            if (EndMarker.FrameNumber < NextStartMarker.FrameNumber)
            {
                // Cut at start of index 0 and start of index 1
                CutEndMarker = NextStartMarker;
            }
        }
    }
    // if subindex is last
    // if end of index - 1 is same as start of index
        // cut at start and end of index
    // if end of index - 1 is earlier than start of index
        // cut at end of index - 1 and end of index
    else if (bIsLastSubIndex)
    {
        // Find end marker of previous index (SubIndex - 1)
        int32 PrevEndIdx = INDEX_NONE;
        for (int32 i = StartMarkerIndex - 1; i >= 0; --i)
        {
            if (SortedMarkers[i].Label.StartsWith(TEXT("e:")))
            {
                PrevEndIdx = i;
                break;
            }
        }

        if (PrevEndIdx != INDEX_NONE)
        {
            const FMovieSceneMarkedFrame& PrevEndMarker = SortedMarkers[PrevEndIdx];

            // If end of index - 1 is earlier than start of current index
            if (PrevEndMarker.FrameNumber < StartMarker.FrameNumber)
            {
                // Cut at end of index - 1 and end of current index
                CutStartMarker = PrevEndMarker;
            }
        }
    }

    TArray<FMovieSceneMarkedFrame> RelevantMarkers;
    RelevantMarkers.Add(CutStartMarker);
    RelevantMarkers.Add(CutEndMarker);

    return SplitAnimationSections(Section, RelevantMarkers, SubstitutionContext, SubstitutionRequest);
}


bool FBlendingAutomationSplitter::SplitAnimationSections(
    UMovieSceneSkeletalAnimationSection* Section, 
    const TArray<FMovieSceneMarkedFrame>& RelevantMarkers,
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest) 
{
    UMovieSceneSkeletalAnimationSection* RightSplitSection = Section;
    UMovieSceneSkeletalAnimationSection* OutRightSplitSection = nullptr;
    UMovieSceneSkeletalAnimationSection* OutLeftSplitSection = nullptr;

    FFrameRate TickResolution = SubstitutionContext.TickResolution;
    FFrameRate DisplayRate = SubstitutionContext.DisplayRate;

    int32 MarkerIndex = 0;
    int32 TransitionCounter = 0;
    int32 GlossIndex = 0;

    for (const FMovieSceneMarkedFrame& Marker : RelevantMarkers)
    {
        FFrameTime DisplayTime = FFrameRate::TransformTime(
            FFrameTime(Marker.FrameNumber), 
            TickResolution, 
            DisplayRate
        );

        int32 DisplayFrame = DisplayTime.FloorToFrame().Value;

        // convert to display frame for logging
        FFrameTime LowerBoundDisplayTime = FFrameRate::TransformTime(
            FFrameTime(RightSplitSection->GetRange().GetLowerBoundValue().Value), 
            TickResolution, 
            DisplayRate
        );

        FFrameTime UpperBoundDisplayTime = FFrameRate::TransformTime(
            FFrameTime(RightSplitSection->GetRange().GetUpperBoundValue().Value), 
            TickResolution, 
            DisplayRate
        );

        if (DisplayFrame <= LowerBoundDisplayTime.FloorToFrame().Value || DisplayFrame >= UpperBoundDisplayTime.FloorToFrame().Value)
        {
            continue;
        }

        USectionAbstraction::SplitAnimationSection(
            RightSplitSection,
            DisplayFrame, // <-- Pass the 30fps frame (29), NOT 23200
            DisplayRate,
            OutRightSplitSection,
            OutLeftSplitSection,
            false
        );

        if (!OutRightSplitSection || !OutLeftSplitSection)
        {
            UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to split the animation section at marker: %s"), *Marker.Label);
            continue;
        }

        RightSplitSection = OutRightSplitSection; // Continue splitting the right section for subsequent markers

        // only take the part after the : for the label, e.g. "Marker: 1" becomes "1"
        FString ShortLabel = Marker.Label;
        FString DiscardedPrefix;

        // if the prefix is e, that make the label the part after the color, otherwise transition   
        if (Marker.Label.StartsWith(TEXT("e:")))
        {
            Marker.Label.Split(TEXT(":"), &DiscardedPrefix, &ShortLabel);
            SubstitutionContext.MarkerSectionMap.Add(MarkerIndex, USectionAbstraction::CreateSectionLabelEntry(OutLeftSplitSection, ShortLabel, GlossIndex));
            GlossIndex++;
        }
        else
        {   
            // The transitions do not have a gloss index
            ShortLabel = FString::Printf(TEXT("Transition_%d"), TransitionCounter);
            SubstitutionContext.MarkerSectionMap.Add(MarkerIndex, USectionAbstraction::CreateSectionLabelEntry(OutLeftSplitSection, ShortLabel, INDEX_NONE));
            TransitionCounter++;
        }

        MarkerIndex++;
    }

    if (RightSplitSection)
    {
        // The transitions do not have a gloss index
        SubstitutionContext.MarkerSectionMap.Add(MarkerIndex, USectionAbstraction::CreateSectionLabelEntry(RightSplitSection, FString::Printf(TEXT("Transition_%d"), TransitionCounter), INDEX_NONE));
    }

    return true;
}
