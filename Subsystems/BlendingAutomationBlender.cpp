#include "BlendingAutomationBlender.h"
#include "../BlendingAutomationBFL.h"
#include "Editor.h"
#include "UObject/UObjectIterator.h"
#include "SequencerAbstractionBPLibrary.h"


bool FBlendingAutomationBlender::BlendAnimationSectionsAndMatchToBone(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
    int32 SubIndex,
    USkeletalMesh* TargetSkeletalMesh
)
{ 
    // Move the sections to blend together 
    bool bBlendSuccess = BlendAnimationSections(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, 1);
    if (!bBlendSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to blend animation sections."));
        return false;
    }

    // Lock hips to avoid shifts between section
    bool bBoneMatchSuccess = MatchSectionsToBone(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, 1, TargetSkeletalMesh);
    if (!bBoneMatchSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to match sections to bone."));
        return false;
    }

    // Set the playback range for the level sequence correctly
    int32 StartFrame = MarkerSectionMap[0].Section->GetRange().GetLowerBoundValue().Value;
    int32 EndFrame = MarkerSectionMap[MarkerSectionMap.Num() - 1].Section->GetRange().GetUpperBoundValue().Value;

    // Convert to display rate
    FFrameTime StartDisplayTime = FFrameRate::TransformTime(FFrameTime(StartFrame), MovieScene->GetTickResolution(), DisplayRate);
    FFrameTime EndDisplayTime = FFrameRate::TransformTime(FFrameTime(EndFrame), MovieScene->GetTickResolution(), DisplayRate);

    int32 StartDisplayFrame = StartDisplayTime.FloorToFrame().Value;
    int32 EndDisplayFrame = EndDisplayTime.FloorToFrame().Value;

    bool bSetPlaybackRangeSuccess = USequencerAbstractionBPLibrary::SetSequencePlaybackRange(LevelSequence, StartDisplayFrame, EndDisplayFrame);
    if (!bSetPlaybackRangeSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to set playback range for the level sequence."));
    }

    return true;
}

bool FBlendingAutomationBlender::BlendAnimationSections(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
    int32 SectionIndex)
{   
    int32 OverlapPercentage = 20; // percentage of overlap between sections
    FFrameRate TickResolution = MovieScene->GetTickResolution();

    FSectionLabelEntry* HeadEntry = MarkerSectionMap.Find(SectionIndex - 1);
    FSectionLabelEntry* NewEntry = MarkerSectionMap.Find(SectionIndex);
    FSectionLabelEntry* TailEntry = MarkerSectionMap.Find(SectionIndex + 1);

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
    FFrameTime NewStartDisplayTime = FFrameRate::TransformTime(FFrameTime(NewStartFrame), MovieScene->GetTickResolution(), DisplayRate);
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
    int32 DisplayFrameOffset = 10;

    // 2. Transform HeadEndTime to Display Rate, then add 10 display frames
    FFrameTime HeadEndDisplayTime = FFrameRate::TransformTime(HeadEndTime, TickResolution, DisplayRate);
    int32 NewStartFrameOffset = HeadEndDisplayTime.FloorToFrame().Value + DisplayFrameOffset;

    // Move the new section to the new start frame (passing int32 frame)
    FSequenceOpenResult Result;
    bool bMoveSuccessNew = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, NewSection, NewStartFrameOffset, Result);
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
    FFrameTime NewStartDisplayTimeTail = FFrameRate::TransformTime(FFrameTime(NewStartFrameTail), MovieScene->GetTickResolution(), DisplayRate);
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
    FFrameTime NewSectionEndDisplayTime = FFrameRate::TransformTime(NewSectionEndTime, TickResolution, DisplayRate);
    int32 NewEndFrame = NewSectionEndDisplayTime.FloorToFrame().Value + DisplayFrameOffset;
    

    UE_LOG(LogBlendingAuto, Display, TEXT("New start Frame for tail section %d"), NewEndFrame);

    // Move the tail section to the new start frame
    FSequenceOpenResult MoveResult;
    bool bMoveSuccessTail = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, TailSection, NewEndFrame, MoveResult);
    if (!bMoveSuccessTail)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move section %s to start frame %d: %s"), *TailSection->GetName(), NewEndFrame, *MoveResult.Error);
        return false;
    }

    // Mark the sequence as changed so the modification can be saved
    MovieScene->MarkAsChanged();

    return true;
}

bool FBlendingAutomationBlender::MatchSectionsToBone(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        int32 SectionIndex,
        USkeletalMesh* TargetSkeletalMesh
    )
{   
    // Match the sections to the bone before subindex
    for (int32 i = SectionIndex; i < MarkerSectionMap.Num(); i++)
    {
        FSectionLabelEntry* CurrentEntry = MarkerSectionMap.Find(i);
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

        if (TargetSkeletalMesh && GEditor)
        {
            UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();

            for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
            {
                // Check if the component belongs to the current editor world and uses the mesh
                if (It->GetWorld() == EditorWorld && It->GetSkeletalMeshAsset() == TargetSkeletalMesh)
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
        FFrameTime DisplayTime = FFrameRate::TransformTime(CurrentStartTime, MovieScene->GetTickResolution(), DisplayRate);
        USequencerAbstractionBPLibrary::moveSequencerPlayheadToFrame(DisplayTime.FloorToFrame().Value);

        USectionAbstraction::MatchSectionByBone(CurrentSection, TargetComponent, CurrentStartTime, MovieScene->GetTickResolution(), "pelvis");
    }

    return true;
}