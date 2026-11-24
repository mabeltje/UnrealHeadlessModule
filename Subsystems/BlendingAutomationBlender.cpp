/**
 * BlendingAutomationBlender.cpp
 *
 * Implements Stage 4 of the blending automation pipeline:
 *   1. Position donor and tail sections with display-frame transition offsets.
 *   2. Match pelvis bone transforms across section cuts to prevent root/hip pops.
 *   3. Fit the Level Sequence playback range to the resulting timeline bounds.
 */

#include "BlendingAutomationBlender.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "ControlRigBlueprintLegacy.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "Editor.h"
#include "Exporters/AnimSeqExportOption.h"
#include "MovieSceneBindingProxy.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "UObject/UObjectIterator.h"

#include "../BlendingAutomationBFL.h"
#include "SequencerAbstractionBPLibrary.h"

namespace
{
    /** Deletes Control Rig keys strictly inside (GapStartTick, GapEndTick), preserving boundary keys. */
    void RemoveControlRigKeysInTickRange(
        UMovieScene* MovieScene,
        const FGuid& BindingId,
        FFrameNumber GapStartTick,
        FFrameNumber GapEndTick)
    {
        if (!MovieScene || !BindingId.IsValid() || GapEndTick <= GapStartTick)
        {
            return;
        }

        UMovieSceneControlRigParameterTrack* RigTrack = Cast<UMovieSceneControlRigParameterTrack>(
            MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), BindingId)
        );
        if (!RigTrack)
        {
            UE_LOG(LogBlendingAuto, Warning, TEXT("RemoveControlRigKeysInTickRange: No ControlRigParameterTrack found."));
            return;
        }

        // Open (exclusive) bounds preserve the last key of the outgoing clip and first key of the incoming clip
        const TRange<FFrameNumber> DeleteRange(
            TRangeBound<FFrameNumber>::Exclusive(GapStartTick),
            TRangeBound<FFrameNumber>::Exclusive(GapEndTick)
        );

        for (UMovieSceneSection* Section : RigTrack->GetAllSections())
        {
            UMovieSceneControlRigParameterSection* RigSection = Cast<UMovieSceneControlRigParameterSection>(Section);
            if (!RigSection)
            {
                continue;
            }

            RigSection->Modify();
            FMovieSceneChannelProxy& ChannelProxy = RigSection->GetChannelProxy();

            for (FMovieSceneDoubleChannel* Channel : ChannelProxy.GetChannels<FMovieSceneDoubleChannel>())
            {
                TArray<FFrameNumber> KeyTimes;
                TArray<FKeyHandle> KeyHandles;
                Channel->GetKeys(DeleteRange, &KeyTimes, &KeyHandles);

                if (KeyHandles.Num() > 0)
                {
                    Channel->DeleteKeys(KeyHandles);
                }
            }

            for (FMovieSceneFloatChannel* Channel : ChannelProxy.GetChannels<FMovieSceneFloatChannel>())
            {
                TArray<FFrameNumber> KeyTimes;
                TArray<FKeyHandle> KeyHandles;
                Channel->GetKeys(DeleteRange, &KeyTimes, &KeyHandles);

                if (KeyHandles.Num() > 0)
                {
                    Channel->DeleteKeys(KeyHandles);
                }
            }

            RigSection->MarkAsChanged();
        }
    }
}

// ============================================================================
// Stage 4: Blend Sections and Match to Bone Entry Point
// =============================================================================
bool FBlendingAutomationBlender::BlendAnimationSectionsAndMatchToBone(
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest)
{ 
    // TODO: This is a temporary solution, we should implement a more robust method to determine the section index based on the label or other criteria
    const int32 TargetSectionIndex = 1;

    // Clean up any existing Control Rig keys in the gaps between sections to avoid unwanted animation artifacts
    PrepareTracksForSubstitution(SubstitutionContext.MovieScene, SubstitutionContext.SkeletalMeshBindingId);

    // Move the sections to blend together 
    bool bBlendSuccess = BlendAnimationSections(SubstitutionContext, SubstitutionRequest, TargetSectionIndex);
    if (!bBlendSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to blend animation sections."));
        return false;
    }

    // Lock hips to avoid shifts between section
    bool bBoneMatchSuccess = MatchSectionsToBone(SubstitutionContext, SubstitutionRequest, TargetSectionIndex);
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

    // Call the function to bake the binding to the control rig
    if (!BakeBindingToControlRig(SubstitutionContext, SubstitutionRequest))
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to bake binding to Control Rig and clean gap keys."));
        return false;
    }

    // Mark the sequence as changed so the modification can be saved
    SubstitutionContext.MovieScene->MarkAsChanged();


    return true;
}

bool FBlendingAutomationBlender::BakeBindingToControlRig(
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest)
{
    FGuid SkeletalMeshBindingId = SubstitutionContext.SkeletalMeshBindingId;
    TSubclassOf<UControlRig> ControlRigClass = SubstitutionContext.ControlRigClass;

    if (!SkeletalMeshBindingId.IsValid())
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Invalid SkeletalMeshBindingId. Cannot bake to Control Rig."));
        return false;
    }

    if (!ControlRigClass)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Invalid ControlRigClass. Cannot bake to Control Rig."));
        return false;
    }


    // Remove keyframes in the gaps between the sections to avoid unwanted animation artifacts
    TArray<TPair<FFrameNumber, FFrameNumber>> GapTickRanges;
    // get gap bounds
    for (int32 i = 0; i < SubstitutionContext.MarkerSectionMap.Num() - 1; ++i)
    {
        UMovieSceneSection* CurrentSection = SubstitutionContext.MarkerSectionMap[i].Section;
        UMovieSceneSection* NextSection = SubstitutionContext.MarkerSectionMap[i + 1].Section;

        if (CurrentSection && NextSection)
        {
            FFrameNumber CurrentEndTick = CurrentSection->GetRange().GetUpperBoundValue();
            FFrameNumber NextStartTick = NextSection->GetRange().GetLowerBoundValue();

            // Only add the gap if there is a gap between the sections
            if (NextStartTick > CurrentEndTick)
            {
                GapTickRanges.Add(TPair<FFrameNumber, FFrameNumber>(CurrentEndTick, NextStartTick));
            }
        }
    }

    // print the gap ranges for debugging
    for (const auto& GapRange : GapTickRanges)
    {
        // print in frames
        UE_LOG(LogBlendingAuto, Display, TEXT("Gap Range: Start Tick = %d, End Tick = %d"), GapRange.Key.Value, GapRange.Value.Value);
    }

    if (SubstitutionContext.TargetSkeletalMesh && GEditor)
    {
        UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
        for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
        {
            if (It->GetWorld() == EditorWorld && It->GetSkeletalMeshAsset() == SubstitutionContext.TargetSkeletalMesh)
            {
                It->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
                It->TickAnimation(0.0f, false);
                It->RefreshBoneTransforms();
                // printf(TEXT("BakeBindingToControlRig: Refreshed bone transforms for SkeletalMeshComponent: %s"), *It->GetName());
                break;
            }
        }
    }


    bool bReduceKeys = true; // Set to true to reduce keys during baking, false to keep all keys
    float Tolerance = 0.01f; // Tolerance for key reduction, adjust as needed
    bool bResetControls = false; // Set to true to reset controls after baking, false to keep current control values

    bool bBakeSuccess = UBakeAbstraction::BakeBindingToControlRig(
        SkeletalMeshBindingId,
        ControlRigClass,
        bReduceKeys,
        Tolerance,
        bResetControls
    );

    if (!bBakeSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to bake binding to Control Rig."));
        return false;
    }

    // After baking, ensure that the Control Rig track and its sections are active and set to Absolute blend type
    if (UMovieSceneControlRigParameterTrack* RigTrack = Cast<UMovieSceneControlRigParameterTrack>(
        SubstitutionContext.MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), SkeletalMeshBindingId)))
    {
        RigTrack->Modify();
        RigTrack->SetEvalDisabled(false);

        for (UMovieSceneSection* Section : RigTrack->GetAllSections())
        {
            if (Section)
            {
                Section->Modify();
                Section->SetIsActive(true);
                Section->SetBlendType(EMovieSceneBlendType::Absolute); // Force Override instead of Additive
            }
        }
    }

    // Remove keyframes in the gaps between the sections so the Control Rig interpolates smoothly
    for (const TPair<FFrameNumber, FFrameNumber>& GapRange : GapTickRanges)
    {
        RemoveControlRigKeysInTickRange(
            SubstitutionContext.MovieScene,
            SkeletalMeshBindingId,
            GapRange.Key,
            GapRange.Value
        );
    }

    SubstitutionContext.MovieScene->MarkAsChanged();
    return true;


}

void FBlendingAutomationBlender::PrepareTracksForSubstitution(UMovieScene* MovieScene, const FGuid& BindingId)
{
    if (!MovieScene || !BindingId.IsValid())
    {
        return;
    }

    MovieScene->Modify();

    // 1. Remove any existing FKControlRig track from prior substitutions
    TArray<UMovieSceneTrack*> TracksToRemove;
    for (UMovieSceneTrack* Track : MovieScene->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), BindingId))
    {
        TracksToRemove.Add(Track);
    }
    for (UMovieSceneTrack* Track : TracksToRemove)
    {
        MovieScene->RemoveTrack(*Track);
    }

    // 2. Re-activate all Skeletal Animation tracks and sections so bone matching & baking read live animation
    for (UMovieSceneTrack* Track : MovieScene->FindTracks(UMovieSceneSkeletalAnimationTrack::StaticClass(), BindingId))
    {
        Track->Modify();
        Track->SetEvalDisabled(false);

        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (Section)
            {
                Section->Modify();
                Section->SetIsActive(true);
            }
        }
    }
}


bool FBlendingAutomationBlender::BlendAnimationSections(
    FSubstitutionContext& SubstitutionContext,
    const FSubstitutionRequest& SubstitutionRequest,
    int32 SectionIndex)
{   
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
    
    
    // Calculate frametime HeadEndTime + 10
    // 1. Define the offset in display frames
    // TODO: Calculate the gap here
    FFrameTime HeadEndTime = HeadSection->GetRange().GetUpperBoundValue();
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

    FFrameTime NewSectionEndTime = NewSection->GetRange().GetUpperBoundValue();

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

        TargetComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        TargetComponent->TickAnimation(0.0f, false);
        TargetComponent->RefreshBoneTransforms();

        // Move the sequencer playhead to the start time of the current section
        FFrameTime DisplayTime = FFrameRate::TransformTime(CurrentStartTime, SubstitutionContext.TickResolution, SubstitutionContext.DisplayRate);
        USequencerAbstractionBPLibrary::moveSequencerPlayheadToFrame(DisplayTime.FloorToFrame().Value);

        USectionAbstraction::MatchSectionByBone(CurrentSection, TargetComponent, CurrentStartTime, SubstitutionContext.TickResolution, "pelvis");
    }

    return true;
}

