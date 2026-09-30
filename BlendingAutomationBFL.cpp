#include "BlendingAutomationBFL.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Editor.h"

#include "VTTParser.h"
#include "SequencerAbstractionBPLibrary.h"
#include "MocapImportSubsystem.h"
#include "SkeletalSequenceDataAsset.h"

bool UBlendingAutomationBFL::ProcessAnimationSubstitution(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& Label,
    int32 SubIndex,
    UAnimSequence*& OutNewAnimation)
{   
    // Load asset data from SkeletalSequenceDataAsset
    USkeletalSequenceDataAsset* SkeletalSequenceDataAsset = LoadObject<USkeletalSequenceDataAsset>(nullptr, TEXT("/Game/Config/AutomationConfig.AutomationConfig"));
    if (!SkeletalSequenceDataAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load SkeletalSequenceDataAsset."));
        return false;
    }
    
    // Load the target skeleton from the SkeletalSequenceDataAsset
    USkeleton* TargetSkeleton = SkeletalSequenceDataAsset->Skeleton.LoadSynchronous();
    if (!TargetSkeleton)
    {
        UE_LOG(LogTemp, Error, TEXT("Target Skeleton is null. Please ensure the SkeletalSequenceDataAsset has a valid Skeleton reference."));
        return false;
    }

    USkeletalMesh* TargetSkeletalMesh = SkeletalSequenceDataAsset->SkeletalMesh.LoadSynchronous();
    if (!TargetSkeletalMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Target Skeletal Mesh is null. Please ensure the SkeletalSequenceDataAsset has a valid SkeletalMesh reference."));
        return false;
    }
    
    // Obtain the MocapImportSubsystem
    UMocapImportSubsystem* MocapSubsystem = GEditor->GetEditorSubsystem<UMocapImportSubsystem>();
    if (!MocapSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("Could not obtain UMocapImportSubsystem!"));
        return false;
    }
    
    // Validate Input Parameters
    if (!ValidateInputParameters(LevelSequence, OriginalAnimationPath, OriginalAnimationSrtPath, DonorAnimationPath, Label, SubIndex))
    {
        return false;
    }
    
    // Get the MovieScene and DisplayRate from the LevelSequence
    UMovieScene* MovieScene = LevelSequence->GetMovieScene();
    FFrameRate DisplayRate = MovieScene->GetDisplayRate();

    UAnimSequence* originalAnimSequence;
    UAnimSequence* donorAnimSequence;
    bool bLoadSuccess = LoadAnimSequence(MocapSubsystem, OriginalAnimationPath, TargetSkeleton, originalAnimSequence);
    bool bLoadDonorSuccess = LoadAnimSequence(MocapSubsystem, DonorAnimationPath, TargetSkeleton, donorAnimSequence);
    
    if (!bLoadSuccess || !originalAnimSequence)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load original animation sequence from path: %s"), *OriginalAnimationPath);
        return false;
    }
    if (!bLoadDonorSuccess || !donorAnimSequence)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load donor animation sequence from path: %s"), *DonorAnimationPath);
        return false;
    }

    // Get the binding guid for the skeletal mesh track
    FGuid SkeletalMeshBindingId;
    TArray<UMovieSceneSkeletalAnimationTrack*> OutTracks;
    FindAllTracks(MovieScene, OutTracks);
    if (OutTracks.Num() > 0)
    {
        MovieScene->FindTrackBinding(*OutTracks[0], SkeletalMeshBindingId);
    }
    // Save the track
    UMovieSceneSkeletalAnimationTrack* Track = OutTracks[0];

    if (!SkeletalMeshBindingId.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find a valid binding for the skeletal animation track."));
        return false;
    }

    // Reset the level sequence to remove all existing tracks and markers
    bool bResetSuccess = ResetLevelSequence(LevelSequence, MovieScene, FGuid());
    
    // Place markers from the VTT file into the level sequence
    TArray<FMovieSceneMarkedFrame> PlacedMarkers;
    bool bMarkersPlaced = PlaceMarkersFromVTT(OriginalAnimationSrtPath, LevelSequence, MovieScene, DisplayRate, PlacedMarkers);
    if (!bMarkersPlaced)
    {
        UE_LOG(LogTemp, Warning, TEXT("No markers were placed from the VTT file."));
        return false;
    }  
    
    // Load animation into level sequence
    FSequenceOpenResult ResultError;
    UMovieSceneSkeletalAnimationSection* NewSection = USequencerAbstractionBPLibrary::AddAnimSectionToBinding(LevelSequence, SkeletalMeshBindingId, originalAnimSequence, 0, 0, false, ResultError);

    if (NewSection == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to add original animation section to the level sequence."));
        return false;
    }

    // // Set the Sequence Playback Range to match the original animation section
    // int32 StartFrame = NewSection->GetRange().GetLowerBoundValue().Value;
    // int32 EndFrame = NewSection->GetRange().GetUpperBoundValue().Value;

    // FFrameTime StartDisplayTime = FFrameRate::TransformTime(FFrameTime(StartFrame), MovieScene->GetTickResolution(), DisplayRate);
    // FFrameTime EndDisplayTime = FFrameRate::TransformTime(FFrameTime(EndFrame), MovieScene->GetTickResolution(), DisplayRate);

    // int32 StartDisplayFrame = StartDisplayTime.FloorToFrame().Value;
    // int32 EndDisplayFrame = EndDisplayTime.FloorToFrame().Value;

    // UE_LOG(LogTemp, Display, TEXT("Original Animation Section added to Level Sequence: Start Frame: %d, End Frame: %d"), StartDisplayFrame, EndDisplayFrame);

    // bool bSetPlaybackRangeSuccess = USequencerAbstractionBPLibrary::SetSequencePlaybackRange(LevelSequence, StartDisplayFrame, EndDisplayFrame);
    // if (!bSetPlaybackRangeSuccess)
    // {
    //     UE_LOG(LogTemp, Warning, TEXT("Failed to set playback range for the level sequence."));
    // }
    
    // Cut the animation into segments based on the markers
    TMap<int32, FSectionLabelEntry> MarkerSectionMap;
    bool bSplitSuccess = SplitAnimationSections(NewSection, MovieScene, PlacedMarkers, MarkerSectionMap);
    
    if (!bSplitSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to split animation sections based on markers."));
        return false;
    }

    PrintSections(MovieScene);

    // print the marker section map for debugging
    for (const auto& Pair : MarkerSectionMap)
    {
        int32 Index = Pair.Key;
        const FSectionLabelEntry& Entry = Pair.Value;
        FString SectionName = Entry.Section ? Entry.Section->GetName() : TEXT("nullptr");
        UE_LOG(LogTemp, Display, TEXT("MarkerSectionMap - Index: %d, Label: %s, Section: %s, GlossIndex: %d"), Index, *Entry.Label, *SectionName, Entry.GlossIndex);
    }

    // get the index from the marker section map where the glossIndex is equal to the subindex
    int32 SectionIndex = INDEX_NONE;
    for (const auto& Pair : MarkerSectionMap)
    {
        if (Pair.Value.GlossIndex == SubIndex)
        {
            SectionIndex = Pair.Key;
            break;
        }
    }

    if (SectionIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not find a section with GlossIndex %d in MarkerSectionMap."), SubIndex);
        return false;
    }

    // Remove a section from the level sequence
    bool bRemoveSuccess = RemoveSectionFromLevelSequence(LevelSequence, MovieScene, SectionIndex, MarkerSectionMap, DisplayRate);

    if (!bRemoveSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to remove section from level sequence."));
        return false;
    }

    PrintSections(MovieScene);

    // first move the tail sections to fit the new section, otherwise the new section will be placed on new track
    bool bMoveSuccess = FitTailSections(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, SectionIndex, donorAnimSequence);


    // Add the donor animation section to the level sequence
    bool AddSuccess = AddDonorAnimationSection(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, Track, donorAnimSequence, SectionIndex, Label);
    
    if (!AddSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to add donor animation section to the level sequence."));
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("Successfully added donor animation section to the level sequence."));
    
    // move the segments to blend together 
    bool bBlendSuccess = BlendAnimationSections(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, SectionIndex);
    if (!bBlendSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to blend animation sections."));
        return false;
    }

    bool bBoneMatchSuccess = MatchSectionsToBone(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, SectionIndex, TargetSkeletalMesh);
    if (!bBoneMatchSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to match sections to bone."));
        return false;
    }

    bool bBakeSuccess = BakeAnimationSequence(LevelSequence, MovieScene, SkeletalMeshBindingId, Label);
    if (!bBakeSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to bake animation sequence."));
        return false;
    }
    
    // Save the level sequence after modifications
    USequencerAbstractionBPLibrary::SaveAsset(LevelSequence);

    return true;
}

bool UBlendingAutomationBFL::MatchSectionsToBone(
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
            UE_LOG(LogTemp, Warning, TEXT("Could not find entry for SubIndex %d in MarkerSectionMap."), i);
            continue;
        }

        UMovieSceneSkeletalAnimationSection* CurrentSection = Cast<UMovieSceneSkeletalAnimationSection>(CurrentEntry->Section);
        if (!CurrentSection)
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find section for SubIndex %d."), i);
            continue;
        }

        // start frame of the current section
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
            UE_LOG(LogTemp, Warning, TEXT("Could not find a SkeletalMeshComponent in the editor world that uses the target skeletal mesh."));
            continue;
        }

        USectionAbstraction::MatchSectionByBone(CurrentSection, TargetComponent, CurrentStartTime, DisplayRate, "pelvis");
    }
    return true;
}

bool UBlendingAutomationBFL::FitTailSections(ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        int32 SectionIndex,
        UAnimSequence* donorAnimSequence)
{
    // Only move the tail section if it exists, i.e., if SectionIndex is not the last index in the MarkerSectionMap
    if (SectionIndex < MarkerSectionMap.Num() - 1) {
        FSectionLabelEntry* NewEntry = MarkerSectionMap.Find(SectionIndex);
        FSectionLabelEntry* TailEntry = MarkerSectionMap.Find(SectionIndex + 1);

        // get length of the donor animation sequence in frames
        FFrameRate TickResolution = MovieScene->GetTickResolution();
        FFrameTime DonorLength = donorAnimSequence->GetPlayLength() * TickResolution;

        UE_LOG(LogTemp, Log, TEXT("Donor animation length: %d frames"), DonorLength.FloorToFrame().Value);

        if (!NewEntry || !TailEntry)
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find entries for SectionIndex %d or %d in MarkerSectionMap."), SectionIndex, SectionIndex + 1);
            return false;
        }

        // Calculate the gap between sectionindex -1 and sectionindex + 1
        if (SectionIndex > 0) {
            FSectionLabelEntry* HeadEntry = MarkerSectionMap.Find(SectionIndex - 1);
            if (!HeadEntry)
            {
                UE_LOG(LogTemp, Warning, TEXT("Could not find entry for SectionIndex %d in MarkerSectionMap."), SectionIndex - 1);
                return false;
            }

            UMovieSceneSection* HeadSection = HeadEntry->Section;
            if (!HeadSection)
            {
                UE_LOG(LogTemp, Warning, TEXT("Could not find section for SectionIndex %d."), SectionIndex - 1);
                return false;
            }

            FFrameTime HeadEndTime = HeadSection->GetRange().GetUpperBoundValue();
            FFrameTime TailStartTime = TailEntry->Section->GetRange().GetLowerBoundValue();
            int32 LengthBetweenSections = TailStartTime.FloorToFrame().Value - HeadEndTime.FloorToFrame().Value;

            // if the length between sections is less than the donor length, move the tail section to the right by the difference
            if (LengthBetweenSections < DonorLength.FloorToFrame().Value)
            {
                int32 Difference = DonorLength.FloorToFrame().Value - LengthBetweenSections; // Add 2 to ensure the section starts after the previous one
                // Move the tail section to the right by the difference
                // for each section from sectionindex + 1 to the end of the MarkerSectionMap, move the section to the left by OverlapFramesTail
                for (int32 i = SectionIndex + 1; i < MarkerSectionMap.Num(); i++)
                {
                    FSectionLabelEntry* CurrentEntry = MarkerSectionMap.Find(i);
                    if (!CurrentEntry)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Could not find entry for SectionIndex %d in MarkerSectionMap."), i);
                        continue;
                    }

                    UMovieSceneSection* CurrentSection = CurrentEntry->Section;
                    if (!CurrentSection)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Could not find section for SubIndex %d."), i);
                        continue;
                    }

                    FFrameTime CurrentStartTime = CurrentSection->GetRange().GetLowerBoundValue();
                    int32 NewStartFrameForCurrent = CurrentStartTime.FloorToFrame().Value + Difference;

                    // Convert to the display rate
                    FFrameTime NewStartDisplayTimeForCurrent = FFrameRate::TransformTime(FFrameTime(NewStartFrameForCurrent), MovieScene->GetTickResolution(), DisplayRate);
                    int32 NewStartDisplayFrameForCurrent = NewStartDisplayTimeForCurrent.FloorToFrame().Value + 10;

                    FSequenceOpenResult MoveResult;
                    bool bMoveSuccess = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, CurrentSection, NewStartDisplayFrameForCurrent, MoveResult);
                    if (!bMoveSuccess)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Failed to move section %s to start frame %d: %s"), *CurrentSection->GetName(), NewStartDisplayFrameForCurrent, *MoveResult.Error);
                        return false;
                    }
                }
            }
        }
       
    }
    UE_LOG(LogTemp, Display, TEXT("Successfully moved tail sections to fit the new section."));
    return true;
}

bool UBlendingAutomationBFL::BakeAnimationSequence(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FGuid SkeletalMeshBindingId,
    const FString& Label) 
{
    // Get the editor world context object
    UWorld* World = nullptr;
    if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }

    if (!World && GEngine)
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
            {
                World = Context.World();
                break;
            }
        }
    }

    UObject* WorldContextObject = World;

    // check if the world context object is valid
    if (!WorldContextObject)
    {
        UE_LOG(LogTemp, Error, TEXT("WorldContextObject is null. Cannot proceed with baking the animation."));
        return false;
    }

    USequencerAbstractionBPLibrary::OpenLevelSequenceInSequencer(LevelSequence);

    FString TargetPackagePath = TEXT("/Game/Animations/BlendingAutomation");
    FString NewAssetName = FString::Printf(TEXT("%s_Baked"), *Label);
    FSequenceOpenResult BakeResultError;

    // bake new animation into new animation fbx
    bool bBakeSuccess = USequencerAbstractionBPLibrary::BakeBindingToAnimSequence(LevelSequence, WorldContextObject, SkeletalMeshBindingId, TargetPackagePath, NewAssetName, BakeResultError);

    if (!bBakeSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to bake new animation sequence: %s"), *BakeResultError.Error);
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("Successfully baked new animation sequence: %s"), *NewAssetName);
    return true;
}

bool UBlendingAutomationBFL::BlendAnimationSections(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
    int32 SectionIndex)
{   
    int32 OverlapPercentage = 10; // percentage of overlap between sections

    // move the new section with a certain amount of frames to the left
    // get the end frame of the head section, if SubIndex is 0, then the head section is the new section, do nothing

    if (SectionIndex > 0) {
        
        FSectionLabelEntry* HeadEntry = MarkerSectionMap.Find(SectionIndex - 1);
        FSectionLabelEntry* NewEntry = MarkerSectionMap.Find(SectionIndex);

        if (!HeadEntry || !NewEntry)
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find entries for SectionIndex %d or %d in MarkerSectionMap."), SectionIndex - 1, SectionIndex);
            return false;
        }

        UMovieSceneSection* HeadSection = HeadEntry->Section;
        UMovieSceneSection* NewSection = NewEntry->Section;

        if (!HeadSection || !NewSection)
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find sections for SectionIndex %d or %d."), SectionIndex - 1, SectionIndex);
            return false;
        }

        FFrameTime HeadEndTime = HeadSection->GetRange().GetUpperBoundValue();
        
        // length of the new section in frames
        FFrameTime NewSectionLength = NewSection->GetRange().Size<FFrameNumber>();
        int32 OverlapFramesHead = FMath::RoundToInt(NewSectionLength.FloorToFrame().Value * (OverlapPercentage / 100.0f));

        // Calculate the new start frame for the new section
        int32 NewStartFrame = HeadEndTime.FloorToFrame().Value - OverlapFramesHead;
        
        // Convert to the display rate
        FFrameTime NewStartDisplayTime = FFrameRate::TransformTime(FFrameTime(NewStartFrame), MovieScene->GetTickResolution(), DisplayRate);
        int32 NewStartDisplayFrame = NewStartDisplayTime.FloorToFrame().Value;

        FSequenceOpenResult Result;
        // Move the new section to the new start frame (passing int32 frame)
        bool bMoveSuccess = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, NewSection, NewStartDisplayFrame, Result);
        if (!bMoveSuccess)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to move new section to start frame %d: %s"), NewStartDisplayFrame, *Result.Error);
            return false;
        }
    }

    // Only move the tail section if it exists, i.e., if SectionIndex is not the last index in the MarkerSectionMap
    if (SectionIndex < MarkerSectionMap.Num() - 1) {
        FSectionLabelEntry* NewEntry = MarkerSectionMap.Find(SectionIndex);
        FSectionLabelEntry* TailEntry = MarkerSectionMap.Find(SectionIndex + 1);

        if (!NewEntry || !TailEntry)
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find entries for SectionIndex %d or %d in MarkerSectionMap."), SectionIndex, SectionIndex + 1);
            return false;
        }

        UMovieSceneSection* NewSection = NewEntry->Section;
        UMovieSceneSection* TailSection = TailEntry->Section;

        if (!NewSection || !TailSection)
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find sections for SectionIndex %d or %d."), SectionIndex, SectionIndex + 1);
            return false;
        }

        FFrameTime NewStartTime = NewSection->GetRange().GetLowerBoundValue();
        FFrameTime NewEndTime = NewSection->GetRange().GetUpperBoundValue();
        FFrameTime TailStartTime = TailSection->GetRange().GetLowerBoundValue();
        int32 LengthBetweenSections = TailStartTime.FloorToFrame().Value - NewEndTime.FloorToFrame().Value;

        FFrameTime NewSectionLength = NewSection->GetRange().Size<FFrameNumber>();
        int32 OverlapFramesTail = FMath::RoundToInt(NewSectionLength.FloorToFrame().Value * (OverlapPercentage / 100.0f));


        // for each section from sectionindex + 1 to the end of the MarkerSectionMap, move the section to the left by OverlapFramesTail
        for (int32 i = SectionIndex + 1; i < MarkerSectionMap.Num(); i++)
        {
            FSectionLabelEntry* CurrentEntry = MarkerSectionMap.Find(i);
            if (!CurrentEntry)
            {
                UE_LOG(LogTemp, Warning, TEXT("Could not find entry for SectionIndex %d in MarkerSectionMap."), i);
                continue;
            }

            UMovieSceneSection* CurrentSection = CurrentEntry->Section;
            if (!CurrentSection)
            {
                UE_LOG(LogTemp, Warning, TEXT("Could not find section for SubIndex %d."), i);
                continue;
            }

            FFrameTime CurrentStartTime = CurrentSection->GetRange().GetLowerBoundValue();
            int32 NewStartFrameForCurrent = CurrentStartTime.FloorToFrame().Value - OverlapFramesTail - LengthBetweenSections;

            // Convert to the display rate
            FFrameTime NewStartDisplayTimeForCurrent = FFrameRate::TransformTime(FFrameTime(NewStartFrameForCurrent), MovieScene->GetTickResolution(), DisplayRate);
            int32 NewStartDisplayFrameForCurrent = NewStartDisplayTimeForCurrent.FloorToFrame().Value;

            FSequenceOpenResult MoveResult;
            bool bMoveSuccess = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, CurrentSection, NewStartDisplayFrameForCurrent, MoveResult);
            if (!bMoveSuccess)
            {
                UE_LOG(LogTemp, Warning, TEXT("Failed to move section %s to start frame %d: %s"), *CurrentSection->GetName(), NewStartDisplayFrameForCurrent, *MoveResult.Error);
                return false;
            }
        }
    }
    UE_LOG(LogTemp, Display, TEXT("Successfully blended animation sections."));
    return true;
}

bool UBlendingAutomationBFL::ResetLevelSequence(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FGuid SkeletalMeshBindingId
)
{
    TArray<UMovieSceneSkeletalAnimationSection*> OutSections;
    bool BfoundSections = FindAllSections(MovieScene, OutSections);

    bool bRemoveSuccess = true;
    for (UMovieSceneSkeletalAnimationSection* Section : OutSections)
    {
        FSequenceOpenResult RemoveResult;
        bool bSectionRemoved = USectionAbstraction::RemoveAnimationSection(LevelSequence, Section, RemoveResult);
        if (!bSectionRemoved)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to remove animation section: %s"), *RemoveResult.Error);
            bRemoveSuccess = false;
        }
    }

    MovieScene->DeleteMarkedFrames();

    MovieScene->MarkAsChanged();
    LevelSequence->MarkPackageDirty();

    UE_LOG(LogTemp, Display, TEXT("Level Sequence has been reset. All tracks and markers have been removed."));

    return bRemoveSuccess;
}

bool UBlendingAutomationBFL::AddDonorAnimationSection(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
    UMovieSceneSkeletalAnimationTrack* Track, 
    UAnimSequence* donorAnimSequence,
    int32 SectionIndex,
    const FString& Label
    ) 
{   
    // get the start frame of the section in the subindex from the marker section map
    FSectionLabelEntry* FoundEntry = MarkerSectionMap.Find(SectionIndex);
    
    if (!FoundEntry)
    {
        UE_LOG(LogTemp, Warning, TEXT("No entry found for SectionIndex %d in MarkerSectionMap."), SectionIndex);
        return false;
    }

    // print the found entry for debugging
    FString SectionName = FoundEntry->Section ? FoundEntry->Section->GetName() : TEXT("nullptr");
    UE_LOG(LogTemp, Display, TEXT("Found entry for SectionIndex %d: Section = %s, Label = %s"), SectionIndex, *SectionName, *FoundEntry->Label);   
    
    // get the start frame of the old section
    FFrameRate TickResolution = MovieScene->GetTickResolution();
    int32 StartFrameOldSection = FoundEntry->Section->GetRange().GetLowerBoundValue().Value;

    Track->Modify();
    UMovieSceneSection* NewRawSection = Track->AddNewAnimation(StartFrameOldSection, donorAnimSequence);
    UMovieSceneSkeletalAnimationSection* NewAnimSection = Cast<UMovieSceneSkeletalAnimationSection>(NewRawSection);

    if (!NewAnimSection)
    {
        UE_LOG(LogTemp, Error, TEXT("AddAnimSequenceAtFrame: Failed to add animation section to track."));
        return false;
    }

    // log the details of the new section for debugging
    int32 NewSectionStartFrame = NewAnimSection->GetRange().GetLowerBoundValue().Value;
    int32 NewSectionEndFrame = NewAnimSection->GetRange().GetUpperBoundValue().Value;
    UE_LOG(LogTemp, Display, TEXT("New animation section added: Start Frame = %d, End Frame = %d, Label = %s"), NewSectionStartFrame, NewSectionEndFrame, *Label);

    // Edit the marker section map to point to the new section
    FoundEntry->Section = NewAnimSection;
    FoundEntry->Label = Label;

    // 3. Mark sequence dirty so the modification can be saved
    Track->MarkAsChanged();
    MovieScene->MarkAsChanged();
    LevelSequence->MarkPackageDirty();

    return true;
}

void UBlendingAutomationBFL::PrintSections(UMovieScene* MovieScene)
{
    TArray<UMovieSceneSkeletalAnimationSection*> AnimSections;
    bool bFoundSections = FindAllSections(MovieScene, AnimSections);

    if (!bFoundSections || AnimSections.Num() == 0)
    {
       UE_LOG(LogTemp, Warning, TEXT("No UMovieSceneSkeletalAnimationSection found in the Level Sequence!"));
    }

    for (UMovieSceneSkeletalAnimationSection* Section : AnimSections)
    {
       if (Section)
       {
           UE_LOG(LogTemp, Display, TEXT("Found Skeletal Animation Section: %s"), *Section->GetName());
           UE_LOG(LogTemp, Display, TEXT("   - Section Range: %d to %d"), 
               Section->GetRange().GetLowerBoundValue().Value, 
               Section->GetRange().GetUpperBoundValue().Value);
       } 
       else
       {
           UE_LOG(LogTemp, Warning, TEXT("No UMovieSceneSkeletalAnimationSection found in the Level Sequence!"));
       }
    }
}

bool UBlendingAutomationBFL::RemoveSectionFromLevelSequence(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        int32 SubIndex,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        FFrameRate DisplayRate
    )
{   
    // Remove section
    FSectionLabelEntry* FoundEntry = MarkerSectionMap.Find(SubIndex);

    // Print the found entry for debugging
    if (!FoundEntry)
    {   
        UE_LOG(LogTemp, Warning, TEXT("No entry found for SubIndex %d in MarkerSectionMap."), SubIndex);
        return false;
    }

        
    UMovieSceneSkeletalAnimationSection* SectionToRemove = Cast<UMovieSceneSkeletalAnimationSection>(FoundEntry->Section.Get());
    
    // get end frame of the section to remove
    if (!SectionToRemove)
    {
        UE_LOG(LogTemp, Warning, TEXT("Section to remove is null."));
        return false;
    }

    FFrameRate TickResolution = MovieScene->GetTickResolution();
    FFrameTime SectionEndTime = SectionToRemove->GetRange().GetUpperBoundValue();
    FFrameTime SectionEndDisplayTime = FFrameRate::TransformTime(SectionEndTime, TickResolution, DisplayRate);
    int32 SectionEndDisplayFrame = SectionEndDisplayTime.FloorToFrame().Value;

    FSequenceOpenResult RemoveResult;
    bool bRemoveSuccess = USectionAbstraction::RemoveAnimationSection(LevelSequence, SectionToRemove, RemoveResult);

    if (!bRemoveSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to remove animation section: %s"), *RemoveResult.Error);
        return false;
    }

    UE_LOG(LogTemp, Display,  TEXT("Successfully removed section for SubIndex %d: Label: %s, Section: %s"), 
        SubIndex, 
        *FoundEntry->Label, 
        SectionToRemove ? *SectionToRemove->GetName() : TEXT("nullptr"));

    return true;
}

bool UBlendingAutomationBFL::PlaceMarkersFromVTT(
    const FString& OriginalAnimationSrtPath,
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    TArray<FMovieSceneMarkedFrame>& PlacedMarkers) 
{   
    TArray<FVTTEntry> OutMarkers;
    bool bSuccess = UVTTParser::ParseVTTFile(OriginalAnimationSrtPath, OutMarkers);

    if (!bSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to parse VTT file."));
    }

    
    for (FVTTEntry& Marker : OutMarkers)
    {
        UVTTParser::UpdateVTTEntryFrames(Marker, DisplayRate); 

        // Add markers to the level sequence
        TArray<FMovieSceneMarkedFrame> NewMarkers = UVTTParser::AddVTTEntryMarkers(LevelSequence, Marker.StartFrame.Value, Marker.EndFrame.Value, Marker.Text);
        PlacedMarkers.Append(NewMarkers);
    }

    // print out the VTT entries for debugging
    for (const FVTTEntry& Marker : OutMarkers)
    {
        UE_LOG(LogTemp, Display, TEXT("Parsed VTT Entry: StartFrame: %d, EndFrame: %d, StartTime: %f, EndTime: %f, Text: %s"), 
            Marker.StartFrame.Value, 
            Marker.EndFrame.Value, 
            Marker.StartTimeSeconds,
            Marker.EndTimeSeconds,
            *Marker.Text);
    }
    
    UE_LOG(LogTemp, Display, TEXT("   - Total Markers Placed: %d"), PlacedMarkers.Num());  

    return bSuccess;
}


bool UBlendingAutomationBFL::LoadAnimSequence(UMocapImportSubsystem* MocapSubsystem, 
    const FString& AnimationPath, 
    USkeleton* TargetSkeleton, 
    UAnimSequence*& OutAnimSequence) {
    
    OutAnimSequence = nullptr;

    if (!MocapSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("LoadAnimSequence: MocapSubsystem is null."));
        return false;
    }

    const FString TargetPath = TEXT("/Game/Animations/BlendingAutomation");
    FString outFirstImportedAssetPath;

    bool importSuccess = MocapSubsystem->importAnimationFromFbx(
        AnimationPath,
        TargetPath,
        TargetSkeleton,
        true,
        outFirstImportedAssetPath
    );
    
    UE_LOG(LogTemp, Display, TEXT("Imported Asset Path: %s"), *outFirstImportedAssetPath);

    if (importSuccess && !outFirstImportedAssetPath.IsEmpty())
    {
        OutAnimSequence = LoadObject<UAnimSequence>(nullptr, *outFirstImportedAssetPath);
    }

    return importSuccess && (OutAnimSequence != nullptr);

}

bool UBlendingAutomationBFL::ValidateInputParameters(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& Label,
    int32 SubIndex)
{
    if (!LevelSequence)                  { UE_LOG(LogTemp, Error, TEXT("LevelSequence is null.")); return false; }
    if (OriginalAnimationPath.IsEmpty())   { UE_LOG(LogTemp, Error, TEXT("OriginalAnimationPath is empty.")); return false; }
    if (OriginalAnimationSrtPath.IsEmpty()){ UE_LOG(LogTemp, Error, TEXT("OriginalAnimationSrtPath is empty.")); return false; }
    if (DonorAnimationPath.IsEmpty())      { UE_LOG(LogTemp, Error, TEXT("DonorAnimationPath is empty.")); return false; }
    if (Label.IsEmpty())                  { UE_LOG(LogTemp, Error, TEXT("Label is empty.")); return false; }
    if (SubIndex < 0)                     { UE_LOG(LogTemp, Error, TEXT("SubIndex is negative.")); return false; }

    return true;
}

bool UBlendingAutomationBFL::FindAllTracks(UMovieScene* MovieScene, TArray<UMovieSceneSkeletalAnimationTrack*>& OutTracks) {
    // Iterate through all object bindings (Actors / Components) in Sequencer
    const UMovieScene* ConstMovieScene = MovieScene;
    for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
    {
        for (UMovieSceneTrack* Track : MovieScene->FindTracks(UMovieSceneSkeletalAnimationTrack::StaticClass(), Binding.GetObjectGuid()))
        {
            if (Track)
            {
                OutTracks.Add(Cast<UMovieSceneSkeletalAnimationTrack>(Track));
            }
        }
    }
    return true;
}

bool UBlendingAutomationBFL::FindAllSections(UMovieScene* MovieScene, TArray<UMovieSceneSkeletalAnimationSection*>& OutSections) {
    TArray<UMovieSceneSkeletalAnimationTrack*> Tracks;
    FindAllTracks(MovieScene, Tracks);

    for (UMovieSceneSkeletalAnimationTrack* Track : Tracks)
    {
        if (Track)
        {
            for (UMovieSceneSection* Section : Track->GetAllSections())
            {
                if (UMovieSceneSkeletalAnimationSection* TypedSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
                {
                    OutSections.Add(TypedSection);
                }
            }
        }
    }
    return true;
 }

bool UBlendingAutomationBFL::SplitAnimationSections(UMovieSceneSkeletalAnimationSection* Section, UMovieScene* MovieScene, const TArray<FMovieSceneMarkedFrame>& Markers, TMap<int32, FSectionLabelEntry>& MarkerSectionMap) {
    UMovieSceneSkeletalAnimationSection* RightSplitSection = Section;
    UMovieSceneSkeletalAnimationSection* OutRightSplitSection = nullptr;
    UMovieSceneSkeletalAnimationSection* OutLeftSplitSection = nullptr;

    FFrameRate TickResolution = MovieScene->GetTickResolution();
    FFrameRate DisplayRate = MovieScene->GetDisplayRate();

    int32 MarkerIndex = 0;
    int32 TransitionCounter = 0;
    int32 GlossIndex = 0;

    for (const FMovieSceneMarkedFrame& Marker : Markers)
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
            // UE_LOG(LogTemp, Warning, TEXT("Marker %s is outside the bounds of the section. Skipping split."), *Marker.Label);
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
            UE_LOG(LogTemp, Warning, TEXT("Failed to split the animation section at marker: %s"), *Marker.Label);
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
            MarkerSectionMap.Add(MarkerIndex, USectionAbstraction::CreateSectionLabelEntry(OutLeftSplitSection, ShortLabel, GlossIndex));
            GlossIndex++;
        }
        else
        {   
            // The transitions do not have a gloss index
            ShortLabel = FString::Printf(TEXT("Transition_%d"), TransitionCounter);
            MarkerSectionMap.Add(MarkerIndex, USectionAbstraction::CreateSectionLabelEntry(OutLeftSplitSection, ShortLabel, INDEX_NONE));
            TransitionCounter++;
        }

        MarkerIndex++;
    }

    if (RightSplitSection)
    {
        // The transitions do not have a gloss index
        MarkerSectionMap.Add(MarkerIndex, USectionAbstraction::CreateSectionLabelEntry(RightSplitSection, FString::Printf(TEXT("Transition_%d"), TransitionCounter), INDEX_NONE));
    }

    UE_LOG(LogTemp, Display, TEXT("Split animation sections based on markers. Total sections created: %d"), MarkerSectionMap.Num());

    return true;
}
