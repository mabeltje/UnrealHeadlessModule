/**
 * BlendingAutomationBlender.cpp
 *
 * Implements Stage 4 of the blending automation pipeline:
 *   1. Position donor and tail sections with display-frame transition offsets.
 *   2. Match pelvis bone transforms across section cuts to prevent root/hip pops.
 *   3. Fit the Level Sequence playback range to the resulting timeline bounds.
 */

#include "BlendingAutomationBlender.h"

#include "Editor.h"
#include "UObject/UObjectIterator.h"

#include "../BlendingAutomationBFL.h"
#include "SequencerAbstractionBPLibrary.h"


bool FBlendingAutomationBlender::BlendAnimationSectionsAndMatchToBone(
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest)
{ 
    // Move the sections to blend together 
    // TODO: This is a temporary solution, we should implement a more robust method to determine the section index based on the label or other criteria
    bool bBlendSuccess = BlendAnimationSections(SubstitutionContext, SubstitutionRequest, 1);
    if (!bBlendSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to blend animation sections."));
        return false;
    }

    // Lock hips to avoid shifts between section
    bool bBoneMatchSuccess = MatchSectionsToBone(SubstitutionContext, SubstitutionRequest, 1);
    if (!bBoneMatchSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to match sections to bone."));
        return false;
    }

    // Set the playback range for the level sequence correctly
    int32 StartFrame = SubstitutionContext.MarkerSectionMap[0].Section->GetRange().GetLowerBoundValue().Value;
    int32 EndFrame = SubstitutionContext.MarkerSectionMap[SubstitutionContext.MarkerSectionMap.Num() - 1].Section->GetRange().GetUpperBoundValue().Value;

    // Convert to display rate
    FFrameTime StartDisplayTime = FFrameRate::TransformTime(FFrameTime(StartFrame), SubstitutionContext.MovieScene->GetTickResolution(), SubstitutionContext.DisplayRate);
    FFrameTime EndDisplayTime = FFrameRate::TransformTime(FFrameTime(EndFrame), SubstitutionContext.MovieScene->GetTickResolution(), SubstitutionContext.DisplayRate);

    int32 StartDisplayFrame = StartDisplayTime.FloorToFrame().Value;
    int32 EndDisplayFrame = EndDisplayTime.FloorToFrame().Value;

    bool bSetPlaybackRangeSuccess = USequencerAbstractionBPLibrary::SetSequencePlaybackRange(SubstitutionRequest.LevelSequence, StartDisplayFrame, EndDisplayFrame);
    if (!bSetPlaybackRangeSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to set playback range for the level sequence."));
    }

    return true;
}


bool FBlendingAutomationBlender::BlendAnimationSections(
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest,
    int32 SectionIndex)
{   
    // TODO: Do not hardcode this here
    int32 OverlapPercentage = 20; // percentage of overlap between sections

    FSectionLabelEntry* HeadEntry = SubstitutionContext.MarkerSectionMap.Find(SectionIndex - 1);
    FSectionLabelEntry* NewEntry = SubstitutionContext.MarkerSectionMap.Find(SectionIndex);
    FSectionLabelEntry* TailEntry = SubstitutionContext.MarkerSectionMap.Find(SectionIndex + 1);

    if (!HeadEntry || !NewEntry || !TailEntry)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entries for SectionIndex %d, %d, or %d in MarkerSectionMap."), SectionIndex - 1, SectionIndex, SectionIndex + 1);
        return false;
    }

    UMovieSceneSection* HeadSection = HeadEntry->Section;
    UMovieSceneSection* NewSection = NewEntry->Section;
    UMovieSceneSection* TailSection = TailEntry->Section;

    if (!HeadSection || !NewSection || !TailSection)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find sections for SectionIndex %d, %d, or %d."), SectionIndex - 1, SectionIndex, SectionIndex + 1);
        return false;
    }
    
    FFrameTime NewSectionLength = NewSection->GetRange().Size<FFrameNumber>();
    FFrameTime HeadEndTime = HeadSection->GetRange().GetUpperBoundValue();
    int32 OverlapFrames = FMath::RoundToInt(NewSectionLength.FloorToFrame().Value * (OverlapPercentage / 100.0f));

    // Calculate the new start frame for the new section
    int32 NewStartFrame = HeadEndTime.FloorToFrame().Value - OverlapFrames;

    // Convert to the display rate
    FFrameTime NewStartDisplayTime = FFrameRate::TransformTime(FFrameTime(NewStartFrame), SubstitutionContext.TickResolution, SubstitutionContext.DisplayRate);
    int32 NewStartDisplayFrame = NewStartDisplayTime.FloorToFrame().Value;

    // // Move the new section to the new start frame (passing int32 frame)
    // FSequenceOpenResult Result;
    // bool bMoveSuccessNew = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, NewSection, NewStartDisplayFrame, Result);
    // if (!bMoveSuccessNew)
    // {
    //     UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move new section to start frame %d: %s"), NewStartDisplayFrame, *Result.Error);
    //     return false;
    // }

    // Calculate frametime HeadEndTime + 10
    // 1. Define the offset in display frames
    // TODO: Temp solution, fix this
    int32 DisplayFrameOffset = 10;

    // 2. Transform HeadEndTime to Display Rate, then add 10 display frames
    FFrameTime HeadEndDisplayTime = FFrameRate::TransformTime(HeadEndTime, SubstitutionContext.TickResolution, SubstitutionContext.DisplayRate);
    int32 NewStartFrameOffset = HeadEndDisplayTime.FloorToFrame().Value + DisplayFrameOffset;

    // Move the new section to the new start frame (passing int32 frame)
    FSequenceOpenResult Result;
    bool bMoveSuccessNew = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(SubstitutionRequest.LevelSequence, NewSection, NewStartFrameOffset, Result);
    if (!bMoveSuccessNew)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move new section to start frame %d: %s"), NewStartFrameOffset, *Result.Error);
        return false;
    }

    UE_LOG(LogBlendingAuto, Display, TEXT("New start Frame for new section %d"), NewStartFrameOffset);

    FFrameTime TailStartTime = TailSection->GetRange().GetLowerBoundValue();
    FFrameTime NewSectionEndTime = NewSection->GetRange().GetUpperBoundValue();
    FFrameTime LengthBetweenSections = TailStartTime - NewSectionEndTime;
    FFrameTime NewStartFrameTail = TailStartTime - FFrameTime(OverlapFrames) - LengthBetweenSections;

    // Convert to the display rate
    FFrameTime NewStartDisplayTimeTail = FFrameRate::TransformTime(FFrameTime(NewStartFrameTail), SubstitutionContext.TickResolution, SubstitutionContext.DisplayRate);
    int32 NewStartDisplayFrameTail = NewStartDisplayTimeTail.FloorToFrame().Value;

    // // Move the tail section to the new start frame
    // FSequenceOpenResult MoveResult;
    // bool bMoveSuccessTail = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, TailSection, NewStartDisplayFrameTail, MoveResult);
    // if (!bMoveSuccessTail)
    // {
    //     UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move section %s to start frame %d: %s"), *TailSection->GetName(), NewStartDisplayFrameTail, *MoveResult.Error);
    //     return false;
    // }

    // Transform NewSectionEndTime to Display Rate before adding the offset
    FFrameTime NewSectionEndDisplayTime = FFrameRate::TransformTime(NewSectionEndTime, SubstitutionContext.TickResolution, SubstitutionContext.DisplayRate);
    int32 NewEndFrame = NewSectionEndDisplayTime.FloorToFrame().Value + DisplayFrameOffset;
    

    UE_LOG(LogBlendingAuto, Display, TEXT("New start Frame for tail section %d"), NewEndFrame);

    // Move the tail section to the new start frame
    FSequenceOpenResult MoveResult;
    bool bMoveSuccessTail = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(SubstitutionRequest.LevelSequence, TailSection, NewEndFrame, MoveResult);
    if (!bMoveSuccessTail)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move section %s to start frame %d: %s"), *TailSection->GetName(), NewEndFrame, *MoveResult.Error);
        return false;
    }

    // Mark the sequence as changed so the modification can be saved
    SubstitutionContext.MovieScene->MarkAsChanged();

    return true;
}


bool FBlendingAutomationBlender::MatchSectionsToBone(
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest,
        int32 SectionIndex
    )
{   
    // Match the sections to the bone before subindex
    for (int32 i = SectionIndex; i < SubstitutionContext.MarkerSectionMap.Num(); i++)
    {
        FSectionLabelEntry* CurrentEntry = SubstitutionContext.MarkerSectionMap.Find(i);
        if (!CurrentEntry)
        {
            UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entry for SubIndex %d in MarkerSectionMap."), i);
            continue;
        }

        UMovieSceneSkeletalAnimationSection* CurrentSection = Cast<UMovieSceneSkeletalAnimationSection>(CurrentEntry->Section);
        if (!CurrentSection)
        {
            UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find section for SubIndex %d."), i);
            continue;
        }

        // Start frame of the current section
        FFrameTime CurrentStartTime = CurrentSection->GetRange().GetLowerBoundValue();
        USkeletalMeshComponent* TargetComponent = nullptr;

        if (SubstitutionContext.TargetSkeletalMesh && GEditor)
        {
            UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();

            for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
            {
                // Check if the component belongs to the current editor world and uses the mesh
                if (It->GetWorld() == EditorWorld && It->GetSkeletalMeshAsset() == SubstitutionContext.TargetSkeletalMesh)
                {
                    TargetComponent = *It;
                    break;
                }
            }
        }

        if (!TargetComponent)
        {
            UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find a SkeletalMeshComponent in the editor world that uses the target skeletal mesh."));
            continue;
        }

        // Move the sequencer playhead to the start time of the current section
        FFrameTime DisplayTime = FFrameRate::TransformTime(CurrentStartTime, SubstitutionContext.TickResolution, SubstitutionContext.DisplayRate);
        USequencerAbstractionBPLibrary::moveSequencerPlayheadToFrame(DisplayTime.FloorToFrame().Value);

        USectionAbstraction::MatchSectionByBone(CurrentSection, TargetComponent, CurrentStartTime, SubstitutionContext.TickResolution, "pelvis");
    }

    return true;
}