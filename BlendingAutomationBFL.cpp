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
#include "AnimationExport.h"
#include "MocapImportSubsystem.h"
#include "SkeletalSequenceDataAsset.h"

DEFINE_LOG_CATEGORY(LogBlendingAuto);

bool UBlendingAutomationBFL::ProcessAnimationSubstitution(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& Label,
    const FString& OutputAnimationPath,
    int32 SubIndex,
    UAnimSequence*& OutNewAnimation)
{   
    UE_LOG(LogBlendingAuto, Display, TEXT("[Substitution Start] SubIndex: %d | Label: '%s'"), SubIndex, *Label);
    UE_LOG(LogBlendingAuto, Display, TEXT("   Original: %s"), *OriginalAnimationPath);
    UE_LOG(LogBlendingAuto, Display, TEXT("   Donor:    %s"), *DonorAnimationPath);
    
    // --- Stage 1: Load and Validate ---
    OutNewAnimation = nullptr;
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
    UE_LOG(LogBlendingAuto, Display, TEXT("     [1/5] Loading & Validating assets..."));

    // --- Stage 2: Place Markers and Split Sections ---
    if (!PlaceMarkersAndSplit(LevelSequence, MovieScene, DisplayRate, OriginalAnimationSrtPath, OriginalAnim, SkeletalMeshBindingId, SubIndex, Label, MarkerSectionMap))
    {
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("     [2/5] Parsing markers & splitting timeline..."));

    // --- Stage 3: Remove Section and Add Donor Animation ---
    if (!ReplaceSection(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, Track, DonorAnim, SubIndex, Label))
    {
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("     [3/5] Replacing section with donor animation..."));

    // --- Stage 4: Blend Sections and Match to Bone ---
    if (!BlendAnimationSectionsAndMatchToBone(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, SubIndex, TargetSkeletalMesh))
    {
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("     [4/5] Blending overlaps & matching pelvis bone transforms..."));

    // --- Stage 5: Bake and Save ---
    if (!BakeAnimationSequence(LevelSequence, MovieScene, SkeletalMeshBindingId, Label))
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to bake animation sequence."));
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("     [5/5] Baking final sequence to asset..."));

    FString BakedAssetPath = FString::Printf(TEXT("/Game/Animations/BlendingAutomation/%s_Baked.%s_Baked"), *Label, *Label);
    OutNewAnimation = LoadObject<UAnimSequence>(nullptr, *BakedAssetPath);
    
    USequencerAbstractionBPLibrary::SaveAsset(LevelSequence);

    // 1. Get the folder of the original animation FBX
    FString CleanExportDir = FPaths::ConvertRelativePathToFull(OutputAnimationPath);

    // get the last part of the output path to use in the name
    FString ExportFileNameBase = FPaths::GetBaseFilename(OutputAnimationPath);

    // 2. Set export file name (e.g. "PLASSEN_baked")
    FString ExportFileName = FString::Printf(TEXT("%s_%s"), *ExportFileNameBase, *Label);

    // 3. Export FBX
    FAnimExportResult ExportResult = UAnimationExport::exportAnimationToFbx(
        BakedAssetPath,
        CleanExportDir,
        ExportFileName,
        false,  // binary FBX
        false,  // forceFrontXAxis
        false,  // exportPreviewMesh
        TEXT("")
    );

    if (!ExportResult.success)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to export FBX: %s"), *ExportResult.message);
    }

    UE_LOG(LogBlendingAuto, Display, TEXT("[Substitution Done] Successfully completed substitution for '%s'."), *Label);
    UE_LOG(LogBlendingAuto, Display, TEXT(""));

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
    // Hardcode the SectionIndex to the SectionIndex, because we now only split the animation in 3 sections
    int32 SectionIndex = 1;

    // Remove a section from the level sequence
    bool bRemoveSuccess = RemoveSectionFromLevelSequence(LevelSequence, MovieScene, SectionIndex, MarkerSectionMap, DisplayRate);
    if (!bRemoveSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to remove section from level sequence."));
        return false;
    }

    // First move the tail section to fit the new section, otherwise the new section will be placed on new track
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
    // TODO: Also look if the transition before and after the subindex section needs to be removed
    // Cut the animation into segments based on the markers, but only split before and after the subindex
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

        // Get length of the donor animation sequence in frames
        // TODO: get tickresolution from the inialized variable
        FFrameRate TickResolution = MovieScene->GetTickResolution();
        FFrameTime DonorLength = donorAnimSequence->GetPlayLength() * TickResolution;

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

            // TODO: it is not necessary to do this in a for loop.
            if (LengthBetweenSections < DonorLength.FloorToFrame().Value)
            {
                int32 Difference = DonorLength.FloorToFrame().Value - LengthBetweenSections; // Add 2 to ensure the section starts after the previous one
                // Move the tail section to the right by the difference
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
                    // TODO: Find out if this is engough for all cases
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

    // Check if the world context object is valid
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

    // Bake new animation into new animation fbx
    bool bBakeSuccess = USequencerAbstractionBPLibrary::BakeBindingToAnimSequence(LevelSequence, WorldContextObject, SkeletalMeshBindingId, TargetPackagePath, NewAssetName, BakeResultError);

    if (!bBakeSuccess)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to bake new animation sequence: %s"), *BakeResultError.Error);
        return false;
    }

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

    return true;
}
