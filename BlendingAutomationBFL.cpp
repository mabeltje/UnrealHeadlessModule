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

DEFINE_LOG_CATEGORY(LogBlendingAuto);

bool UBlendingAutomationBFL::ProcessAnimationSubstitution(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& Label,
    int32 SubIndex,
    UAnimSequence*& OutNewAnimation)
{   

    OutNewAnimation = nullptr;

    // --- Stage 1: Load and Validate ---
    USkeleton* TargetSkeleton = nullptr;
    USkeletalMesh* TargetSkeletalMesh = nullptr;
    UMovieScene* MovieScene = nullptr;
    FFrameRate DisplayRate;
    FFrameRate TickResolution;
    UAnimSequence* OriginalAnim = nullptr;
    UAnimSequence* DonorAnim = nullptr;
    UMocapImportSubsystem* MocapSubsystem = nullptr;
    UMovieSceneSkeletalAnimationTrack* Track = nullptr;
    FGuid SkeletalMeshBindingId;
    TMap<int32, FSectionLabelEntry> MarkerSectionMap;

    if (!LoadAndValidate(
            LevelSequence, OriginalAnimationPath, OriginalAnimationSrtPath, DonorAnimationPath, Label, SubIndex,
            TargetSkeleton, TargetSkeletalMesh, MovieScene, DisplayRate, TickResolution,
            OriginalAnim, DonorAnim, MocapSubsystem, Track, SkeletalMeshBindingId))
    {
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("Successfully loaded and validated all necessary assets and parameters."));

    // --- Stage 2: Place Markers and Split Sections ---

    if (!PlaceMarkersAndSplit(LevelSequence, MovieScene, DisplayRate, OriginalAnimationSrtPath, OriginalAnim, SkeletalMeshBindingId, SubIndex, Label, MarkerSectionMap))
    {
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("Successfully placed markers and split animation sections."));

    // --- Stage 3: Remove Section and Add Donor Animation ---
    if (!ReplaceSection(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, Track, DonorAnim, SubIndex, Label))
    {
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("Successfully replaced section with donor animation."));

    // --- Stage 4: Blend Sections and Match to Bone ---
    if (!BlendAnimationSectionsAndMatchToBone(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, SubIndex, TargetSkeletalMesh))
    {
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("Successfully blended animation sections and matched bones."));

    // --- Stage 5: Bake and Save ---
    if (!BakeAnimationSequence(LevelSequence, MovieScene, SkeletalMeshBindingId, Label))
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to bake animation sequence."));
        return false;
    }

    FString BakedAssetPath = FString::Printf(TEXT("/Game/Animations/BlendingAutomation/%s_Baked.%s_Baked"), *Label, *Label);
    OutNewAnimation = LoadObject<UAnimSequence>(nullptr, *BakedAssetPath);
    
    USequencerAbstractionBPLibrary::SaveAsset(LevelSequence);
    return (OutNewAnimation != nullptr);
}


bool UBlendingAutomationBFL::BlendAnimationSectionsAndMatchToBone(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
    int32 SubIndex,
    USkeletalMesh* TargetSkeletalMesh
)
{ 
    // move the segments to blend together 
    bool bBlendSuccess = BlendAnimationSections(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, 1);
    if (!bBlendSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to blend animation sections."));
        return false;
    }

    bool bBoneMatchSuccess = MatchSectionsToBone(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, 1, TargetSkeletalMesh);
    if (!bBoneMatchSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to match sections to bone."));
        return false;
    }

    UE_LOG(LogBlendingAuto, Display, TEXT("Successfully matched sections to bone."));

    // get the start frame of the first section and the end frame of the last section in the marker section map

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

bool UBlendingAutomationBFL::ReplaceSection(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
    UMovieSceneSkeletalAnimationTrack* Track, 
    UAnimSequence* donorAnimSequence,
    int32 SubIndex,
    const FString& Label
)
{   
    PrintSections(MovieScene);

    // print the marker section map for debugging
    for (const auto& Pair : MarkerSectionMap)
    {
        int32 Index = Pair.Key;
        const FSectionLabelEntry& Entry = Pair.Value;
        FString SectionName = Entry.Section ? Entry.Section->GetName() : TEXT("nullptr");
        UE_LOG(LogBlendingAuto, Display, TEXT("MarkerSectionMap - Index: %d, Label: %s, Section: %s, GlossIndex: %d"), Index, *Entry.Label, *SectionName, Entry.GlossIndex);
    }

    // get the index from the marker section map where the glossIndex is equal to the subindex
    // int32 SectionIndex = INDEX_NONE;
    // for (const auto& Pair : MarkerSectionMap)
    // {
    //     if (Pair.Value.GlossIndex == SubIndex)
    //     {
    //         SectionIndex = Pair.Key;
    //         break;
    //     }
    // }

    // if (SectionIndex == INDEX_NONE)
    // {
    //     UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find a section with GlossIndex %d in MarkerSectionMap."), SubIndex);
    //     return false;
    // }

    // Hardcode the SectionIndex to the SectionIndex, because we now only split the animation in 3 sections
    int32 SectionIndex = 1;

    // Remove a section from the level sequence
    bool bRemoveSuccess = RemoveSectionFromLevelSequence(LevelSequence, MovieScene, SectionIndex, MarkerSectionMap, DisplayRate);

    if (!bRemoveSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to remove section from level sequence."));
        return false;
    }

    // first move the tail sections to fit the new section, otherwise the new section will be placed on new track
    bool bMoveSuccess = FitTailSections(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, SectionIndex, donorAnimSequence);
    if (!bMoveSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move tail sections to fit the new section."));
        return false;
    }

    // Add the donor animation section to the level sequence
    bool AddSuccess = AddDonorAnimationSection(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, Track, donorAnimSequence, SectionIndex, Label);
    
    if (!AddSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to add donor animation section to the level sequence."));
        return false;
    }

    return true;
}

bool UBlendingAutomationBFL::PlaceMarkersAndSplit(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    const FString& OriginalAnimationSrtPath,
    UAnimSequence* originalAnimSequence,
    FGuid SkeletalMeshBindingId,
    int32 SubIndex,
    const FString& Label,
    TMap<int32, FSectionLabelEntry>& OutMarkerSectionMap
)
{
    // Place markers from the VTT file into the level sequence
    TArray<FMovieSceneMarkedFrame> PlacedMarkers;
    bool bMarkersPlaced = PlaceMarkersFromVTT(OriginalAnimationSrtPath, LevelSequence, MovieScene, DisplayRate, PlacedMarkers);
    if (!bMarkersPlaced)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("No markers were placed from the VTT file."));
        return false;
    }  
    
    // Load animation into level sequence
    FSequenceOpenResult ResultError;
    UMovieSceneSkeletalAnimationSection* NewSection = USequencerAbstractionBPLibrary::AddAnimSectionToBinding(LevelSequence, SkeletalMeshBindingId, originalAnimSequence, 0, 0, false, ResultError);
    
    if (NewSection == nullptr)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to add original animation section to the level sequence."));
        return false;
    }

    // Cut the animation into segments based on the markers
    // bool bSplitSuccess = SplitAnimationSections(NewSection, MovieScene, PlacedMarkers, OutMarkerSectionMap);
    // Now only split the section that matches the subindex
    bool bSplitSuccess = SplitAnimationSection(NewSection, MovieScene, PlacedMarkers, OutMarkerSectionMap, SubIndex, Label);
    
    if (!bSplitSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to split animation sections based on markers."));
        return false;
    }

    return true;
}

bool UBlendingAutomationBFL::LoadAndValidate(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& Label,
    int32 SubIndex,
    USkeleton*& OutSkeleton,
    USkeletalMesh*& OutMesh,
    UMovieScene*& OutMovieScene,
    FFrameRate& OutDisplayRate,
    FFrameRate& OutTickResolution,
    UAnimSequence*& OutOriginalAnim,
    UAnimSequence*& OutDonorAnim,
    UMocapImportSubsystem*& OutMocapSubsystem,
    UMovieSceneSkeletalAnimationTrack*& OutTrack,
    FGuid& OutSkeletalMeshBindingId
)
{   
    // Validate Input Parameters
    if (!ValidateInputParameters(LevelSequence, OriginalAnimationPath, OriginalAnimationSrtPath, DonorAnimationPath, Label, SubIndex))
    {   
        UE_LOG(LogBlendingAuto, Error, TEXT("Input parameter validation failed."));
        return false;
    }

    // Load config
    USkeletalSequenceDataAsset* SkeletalSequenceDataAsset = LoadObject<USkeletalSequenceDataAsset>(nullptr, TEXT("/Game/Config/AutomationConfig.AutomationConfig"));
    if (!SkeletalSequenceDataAsset)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to load SkeletalSequenceDataAsset."));
        return false;
    }
    
    // Load the target skeleton from the SkeletalSequenceDataAsset
    OutSkeleton = SkeletalSequenceDataAsset->Skeleton.LoadSynchronous();
    if (!OutSkeleton)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Target Skeleton is null. Please ensure the SkeletalSequenceDataAsset has a valid Skeleton reference."));
        return false;
    }

    OutMesh = SkeletalSequenceDataAsset->SkeletalMesh.LoadSynchronous();
    if (!OutMesh)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Target Skeletal Mesh is null. Please ensure the SkeletalSequenceDataAsset has a valid SkeletalMesh reference."));
        return false;
    }
    
    // Subsystem
    OutMocapSubsystem = GEditor->GetEditorSubsystem<UMocapImportSubsystem>();
    if (!OutMocapSubsystem)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Could not obtain UMocapImportSubsystem!"));
        return false;
    }
    
    // Get the MovieScene and Rates
    OutMovieScene = LevelSequence->GetMovieScene();
    if (!OutMovieScene)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("LevelSequence does not have a valid MovieScene."));
        return false;
    }
    OutDisplayRate = OutMovieScene->GetDisplayRate();
    OutTickResolution = OutMovieScene->GetTickResolution();

    if (!LoadAnimSequence(OutMocapSubsystem, OriginalAnimationPath, OutSkeleton, OutOriginalAnim))
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to import original animation: %s"), *OriginalAnimationPath);
        return false;
    }

    if (!LoadAnimSequence(OutMocapSubsystem, DonorAnimationPath, OutSkeleton, OutDonorAnim))
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to import donor animation: %s"), *DonorAnimationPath);
        return false;
    }

    // UE_LOG(LogBlendingAuto, Display, TEXT("Successfully loaded original and donor animation sequences."));

    // Get the binding guid for the skeletal mesh track
    TArray<UMovieSceneSkeletalAnimationTrack*> OutTracks;
    FindAllTracks(OutMovieScene, OutTracks);
    if (OutTracks.Num() > 0)
    {
        OutMovieScene->FindTrackBinding(*OutTracks[0], OutSkeletalMeshBindingId);
    }

    // Save the track
    OutTrack = OutTracks[0];

    if (!OutSkeletalMeshBindingId.IsValid())
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to find a valid binding for the skeletal animation track."));
        return false;
    }

    // Reset the level sequence to remove all existing tracks and markers
    bool bResetSuccess = ResetLevelSequence(LevelSequence, OutMovieScene, FGuid());
    if (!bResetSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to reset the level sequence."));
        return false;
    }

    // UE_LOG(LogBlendingAuto, Display, TEXT("Validation and loading completed successfully."));
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
            UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entry for SubIndex %d in MarkerSectionMap."), i);
            continue;
        }

        UMovieSceneSkeletalAnimationSection* CurrentSection = Cast<UMovieSceneSkeletalAnimationSection>(CurrentEntry->Section);
        if (!CurrentSection)
        {
            UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find section for SubIndex %d."), i);
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

        // UE_LOG(LogBlendingAuto, Log, TEXT("Donor animation length: %d frames"), DonorLength.FloorToFrame().Value);

        if (!NewEntry || !TailEntry)
        {
            UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entries for SectionIndex %d or %d in MarkerSectionMap."), SectionIndex, SectionIndex + 1);
            return false;
        }

        // Calculate the gap between sectionindex -1 and sectionindex + 1
        if (SectionIndex > 0) {
            FSectionLabelEntry* HeadEntry = MarkerSectionMap.Find(SectionIndex - 1);
            if (!HeadEntry)
            {
                UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entry for SectionIndex %d in MarkerSectionMap."), SectionIndex - 1);
                return false;
            }

            UMovieSceneSection* HeadSection = HeadEntry->Section;
            if (!HeadSection)
            {
                UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find section for SectionIndex %d."), SectionIndex - 1);
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
                        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entry for SectionIndex %d in MarkerSectionMap."), i);
                        continue;
                    }

                    UMovieSceneSection* CurrentSection = CurrentEntry->Section;
                    if (!CurrentSection)
                    {
                        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find section for SubIndex %d."), i);
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
                        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move section %s to start frame %d: %s"), *CurrentSection->GetName(), NewStartDisplayFrameForCurrent, *MoveResult.Error);
                        return false;
                    }
                }
            }
        }
       
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("Successfully moved tail sections to fit the new section."));
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
        UE_LOG(LogBlendingAuto, Error, TEXT("WorldContextObject is null. Cannot proceed with baking the animation."));
        return false;
    }

    USequencerAbstractionBPLibrary::OpenLevelSequenceInSequencer(LevelSequence);

    FString TargetPackagePath = TEXT("/Game/Animations/BlendingAutomation");
    // TODO: name: originalbase_label_baked, get the name of the original animation sequence and append the label to it
    FString NewAssetName = FString::Printf(TEXT("%s_Baked"), *Label);
    FSequenceOpenResult BakeResultError;

    // bake new animation into new animation fbx
    bool bBakeSuccess = USequencerAbstractionBPLibrary::BakeBindingToAnimSequence(LevelSequence, WorldContextObject, SkeletalMeshBindingId, TargetPackagePath, NewAssetName, BakeResultError);

    if (!bBakeSuccess)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to bake new animation sequence: %s"), *BakeResultError.Error);
        return false;
    }
    // add newline after logging the success message for better readability in the output log
    UE_LOG(LogBlendingAuto, Display, TEXT("Successfully baked new animation sequence: %s") , *NewAssetName);
    return true;
}

bool UBlendingAutomationBFL::BlendAnimationSections(
    ULevelSequence* LevelSequence,
    UMovieScene* MovieScene,
    FFrameRate DisplayRate,
    TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
    int32 SectionIndex)
{   
    int32 OverlapPercentage = 20; // percentage of overlap between sections

    FSectionLabelEntry* HeadEntry = MarkerSectionMap.Find(SectionIndex - 1);
    FSectionLabelEntry* NewEntry = MarkerSectionMap.Find(SectionIndex);
    FSectionLabelEntry* TailEntry = MarkerSectionMap.Find(SectionIndex + 1);
    UMovieSceneSection* HeadSection = HeadEntry->Section;
    UMovieSceneSection* NewSection = NewEntry->Section;
    UMovieSceneSection* TailSection = TailEntry->Section;

    if (!HeadEntry || !NewEntry || !TailEntry)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entries for SectionIndex %d, %d, or %d in MarkerSectionMap."), SectionIndex - 1, SectionIndex, SectionIndex + 1);
        return false;
    }
    
    FFrameTime NewSectionLength = NewSection->GetRange().Size<FFrameNumber>();
    FFrameTime HeadEndTime = HeadSection->GetRange().GetUpperBoundValue();
    int32 OverlapFrames = FMath::RoundToInt(NewSectionLength.FloorToFrame().Value * (OverlapPercentage / 100.0f));

    UE_LOG(LogBlendingAuto, Display, TEXT("HeadEndTime: %d, NewSectionLength: %d, OverlapFrames: %d"), HeadEndTime.FloorToFrame().Value, NewSectionLength.FloorToFrame().Value, OverlapFrames);

    // Calculate the new start frame for the new section
    int32 NewStartFrame = HeadEndTime.FloorToFrame().Value - OverlapFrames;

    // Convert to the display rate
    FFrameTime NewStartDisplayTime = FFrameRate::TransformTime(FFrameTime(NewStartFrame), MovieScene->GetTickResolution(), DisplayRate);
    int32 NewStartDisplayFrame = NewStartDisplayTime.FloorToFrame().Value;

    // Move the new section to the new start frame (passing int32 frame)
    FSequenceOpenResult Result;
    bool bMoveSuccessNew = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, NewSection, NewStartDisplayFrame, Result);
    if (!bMoveSuccessNew)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move new section to start frame %d: %s"), NewStartDisplayFrame, *Result.Error);
        return false;
    }

    FFrameTime TailStartTime = TailSection->GetRange().GetLowerBoundValue();
    FFrameTime NewSectionEndTime = NewSection->GetRange().GetUpperBoundValue();
    FFrameTime LengthBetweenSections = TailStartTime - NewSectionEndTime;

    FFrameTime NewStartFrameTail = TailStartTime - FFrameTime(OverlapFrames) - LengthBetweenSections;

    // Convert to the display rate
    FFrameTime NewStartDisplayTimeTail = FFrameRate::TransformTime(FFrameTime(NewStartFrameTail), MovieScene->GetTickResolution(), DisplayRate);
    int32 NewStartDisplayFrameTail = NewStartDisplayTimeTail.FloorToFrame().Value;

    // Move the tail section to the new start frame
    FSequenceOpenResult MoveResult;
    bool bMoveSuccessTail = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, TailSection, NewStartDisplayFrameTail, MoveResult);
    if (!bMoveSuccessTail)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move section %s to start frame %d: %s"), *TailSection->GetName(), NewStartDisplayFrameTail, *MoveResult.Error);
        return false;
    }

    // Mark the sequence as changed so the modification can be saved
    MovieScene->MarkAsChanged();
    LevelSequence->MarkPackageDirty();

    UE_LOG(LogBlendingAuto, Display, TEXT("Successfully blended animation sections."));
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
            UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to remove animation section: %s"), *RemoveResult.Error);
            bRemoveSuccess = false;
        }
    }

    MovieScene->DeleteMarkedFrames();

    MovieScene->MarkAsChanged();
    LevelSequence->MarkPackageDirty();

    UE_LOG(LogBlendingAuto, Display, TEXT("Level Sequence has been reset. All tracks and markers have been removed."));

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
        UE_LOG(LogBlendingAuto, Warning, TEXT("No entry found for SectionIndex %d in MarkerSectionMap."), SectionIndex);
        return false;
    }

    // print the found entry for debugging
    FString SectionName = FoundEntry->Section ? FoundEntry->Section->GetName() : TEXT("nullptr");
    UE_LOG(LogBlendingAuto, Display, TEXT("Found entry for SectionIndex %d: Section = %s, Label = %s"), SectionIndex, *SectionName, *FoundEntry->Label);   
    
    // get the start frame of the old section
    FFrameRate TickResolution = MovieScene->GetTickResolution();
    int32 StartFrameOldSection = FoundEntry->Section->GetRange().GetLowerBoundValue().Value;

    Track->Modify();
    UMovieSceneSection* NewRawSection = Track->AddNewAnimation(StartFrameOldSection, donorAnimSequence);
    UMovieSceneSkeletalAnimationSection* NewAnimSection = Cast<UMovieSceneSkeletalAnimationSection>(NewRawSection);

    if (!NewAnimSection)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("AddAnimSequenceAtFrame: Failed to add animation section to track."));
        return false;
    }

    // log the details of the new section for debugging
    int32 NewSectionStartFrame = NewAnimSection->GetRange().GetLowerBoundValue().Value;
    int32 NewSectionEndFrame = NewAnimSection->GetRange().GetUpperBoundValue().Value;
    // UE_LOG(LogBlendingAuto, Display, TEXT("New animation section added: Start Frame = %d, End Frame = %d, Label = %s"), NewSectionStartFrame, NewSectionEndFrame, *Label);

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
       UE_LOG(LogBlendingAuto, Warning, TEXT("No UMovieSceneSkeletalAnimationSection found in the Level Sequence!"));
    }

    for (UMovieSceneSkeletalAnimationSection* Section : AnimSections)
    {
       if (Section)
       {
           UE_LOG(LogBlendingAuto, Display, TEXT("Found Skeletal Animation Section: %s"), *Section->GetName());
           UE_LOG(LogBlendingAuto, Display, TEXT("   - Section Range: %d to %d"), 
               Section->GetRange().GetLowerBoundValue().Value, 
               Section->GetRange().GetUpperBoundValue().Value);
       } 
       else
       {
           UE_LOG(LogBlendingAuto, Warning, TEXT("No UMovieSceneSkeletalAnimationSection found in the Level Sequence!"));
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
        UE_LOG(LogBlendingAuto, Warning, TEXT("No entry found for SubIndex %d in MarkerSectionMap."), SubIndex);
        return false;
    }

        
    UMovieSceneSkeletalAnimationSection* SectionToRemove = Cast<UMovieSceneSkeletalAnimationSection>(FoundEntry->Section.Get());
    
    // get end frame of the section to remove
    if (!SectionToRemove)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Section to remove is null."));
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
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to remove animation section: %s"), *RemoveResult.Error);
        return false;
    }

    UE_LOG(LogBlendingAuto, Display,  TEXT("Successfully removed section for SubIndex %d: Label: %s, Section: %s"), 
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
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to parse VTT file."));
    }

    
    for (FVTTEntry& Marker : OutMarkers)
    {
        UVTTParser::UpdateVTTEntryFrames(Marker, DisplayRate); 

        // Add markers to the level sequence
        TArray<FMovieSceneMarkedFrame> NewMarkers = UVTTParser::AddVTTEntryMarkers(LevelSequence, Marker.StartFrame.Value, Marker.EndFrame.Value, Marker.Text);
        PlacedMarkers.Append(NewMarkers);
    }

    // // print out the VTT entries for debugging
    // for (const FVTTEntry& Marker : OutMarkers)
    // {
    //     UE_LOG(LogBlendingAuto, Display, TEXT("Parsed VTT Entry: StartFrame: %d, EndFrame: %d, StartTime: %f, EndTime: %f, Text: %s"), 
    //         Marker.StartFrame.Value, 
    //         Marker.EndFrame.Value, 
    //         Marker.StartTimeSeconds,
    //         Marker.EndTimeSeconds,
    //         *Marker.Text);
    // }
    
    UE_LOG(LogBlendingAuto, Display, TEXT("   - Total Markers Placed: %d"), PlacedMarkers.Num());  

    return bSuccess;
}


bool UBlendingAutomationBFL::LoadAnimSequence(UMocapImportSubsystem* MocapSubsystem, 
    const FString& AnimationPath, 
    USkeleton* TargetSkeleton, 
    UAnimSequence*& OutAnimSequence) {
    
    OutAnimSequence = nullptr;

    if (!MocapSubsystem)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("LoadAnimSequence: MocapSubsystem is null."));
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
    
    // UE_LOG(LogBlendingAuto, Display, TEXT("Imported Asset Path: %s"), *outFirstImportedAssetPath);

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
    if (!LevelSequence)                  { UE_LOG(LogBlendingAuto, Error, TEXT("LevelSequence is null.")); return false; }
    if (OriginalAnimationPath.IsEmpty())   { UE_LOG(LogBlendingAuto, Error, TEXT("OriginalAnimationPath is empty.")); return false; }
    if (OriginalAnimationSrtPath.IsEmpty()){ UE_LOG(LogBlendingAuto, Error, TEXT("OriginalAnimationSrtPath is empty.")); return false; }
    if (DonorAnimationPath.IsEmpty())      { UE_LOG(LogBlendingAuto, Error, TEXT("DonorAnimationPath is empty.")); return false; }
    if (Label.IsEmpty())                  { UE_LOG(LogBlendingAuto, Error, TEXT("Label is empty.")); return false; }
    if (SubIndex < 0)                     { UE_LOG(LogBlendingAuto, Error, TEXT("SubIndex is negative.")); return false; }

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

bool UBlendingAutomationBFL::SplitAnimationSection(UMovieSceneSkeletalAnimationSection* Section, UMovieScene* MovieScene, const TArray<FMovieSceneMarkedFrame>& Markers, TMap<int32, FSectionLabelEntry>& MarkerSectionMap, int32 SubIndex,  const FString& Label) {
    // Find the 2 markers that correspond to the s: and e: labels for the given label 
    FMovieSceneMarkedFrame StartMarker;
    FMovieSceneMarkedFrame EndMarker;
    bool bFoundStart = false;
    bool bFoundEnd = false;

    // This is a temp solution until I find a way to know which marker is the original gloss of the section we have to sub
    int32 GlossIndex = 0;

    for (const FMovieSceneMarkedFrame& Marker : Markers)
    {   
        if (Marker.Label.StartsWith("s:"))
        {   
            if (GlossIndex == SubIndex)
            {
                StartMarker = Marker;
                bFoundStart = true;
            }
        }
        else if (Marker.Label.StartsWith("e:"))
        {   
            if (GlossIndex == SubIndex)
            {
                EndMarker = Marker;
                bFoundEnd = true;
            }
            GlossIndex++;
        }
    }

    if (!bFoundStart || !bFoundEnd)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find start or end markers for SubIndex: %d"), SubIndex);
        return false;
    }

    TArray<FMovieSceneMarkedFrame> RelevantMarkers;
    RelevantMarkers.Add(StartMarker);
    RelevantMarkers.Add(EndMarker);

    return SplitAnimationSections(Section, MovieScene, RelevantMarkers, MarkerSectionMap);
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
            // UE_LOG(LogBlendingAuto, Warning, TEXT("Marker %s is outside the bounds of the section. Skipping split."), *Marker.Label);
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

    UE_LOG(LogBlendingAuto, Display, TEXT("Split animation sections based on markers. Total sections created: %d"), MarkerSectionMap.Num());

    return true;
}
