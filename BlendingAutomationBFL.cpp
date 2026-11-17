/**
 * BlendingAutomationBFL.cpp
 *
 * The main file for the blending automation pipeline.
 * It consists of the main logic for processing animation substitutions.
 * There are 5 stages in the pipeline:
 * 1. Load and Validate: Load the original and donor animations, validate the data, and prepare the data structures.
 * 2. Place Markers and Split Sections: Parse the markers from the original animation's srt file, place them in the level sequence, and split the animation sections accordingly.
 * 3. Remove Section and Add Donor Animation: Remove the original animation section and add the donor animation section to the level sequence.
 * 4. Blend Sections and Match to Bone: Generate yhe new transitions and match the pelvis to avoid shifts between sections.
 * 5. Bake and Save: Bake the final blended animation sequence to a new asset and save it.
 */

#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Editor.h"
#include "SkeletalSequenceDataAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "VTTParser.h"
#include "BlendingAutomationBFL.h"
#include "SequencerAbstractionBPLibrary.h"
#include "AnimationExport.h"
#include "MocapImportSubsystem.h"

#include "Subsystems/BlendingAutomationBlender.h"
#include "Subsystems/BlendingAutomationExporter.h"
#include "Subsystems/BlendingAutomationSplitter.h"


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

    // TODO: have a folder that is wiped after every substitution, with for example the baked control rig data or the imported trimmed fbx
    
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
    if (!FBlendingAutomationSplitter::PlaceMarkersAndSplit(LevelSequence, MovieScene, DisplayRate, OriginalAnimationSrtPath, OriginalAnim, SkeletalMeshBindingId, SubIndex, Label, MarkerSectionMap))
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
    if (!FBlendingAutomationBlender::BlendAnimationSectionsAndMatchToBone(LevelSequence, MovieScene, DisplayRate, MarkerSectionMap, SubIndex, TargetSkeletalMesh))
    {
        return false;
    }
    UE_LOG(LogBlendingAuto, Display, TEXT("     [4/5] Blending overlaps & matching pelvis bone transforms..."));

    // --- Stage 5: Bake and Save ---
    if (!FBlendingAutomationExporter::BakeAnimationSequence(LevelSequence, MovieScene, SkeletalMeshBindingId, Label))
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

    // 2. Set export file name (e.g. "OPA_baked")
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

// Stage 1: Load and Validate
bool UBlendingAutomationBFL::LoadAndValidate(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& Label,
    int32 SubIndex,
    FSubstitutionContext*& OutSubstitutionContext,
    FSubstitutionRequest*& OutSubstitutionRequest,
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
    // Create the substitution context and request structures
    FSubstitutionContext SubstitutionContext;
    FSubstitutionRequest SubstitutionRequest;

    // Populate the substitution request with the input parameters
    SubstitutionRequest.LevelSequence = LevelSequence;
    SubstitutionRequest.OriginalAnimationPath = OriginalAnimationPath;
    SubstitutionRequest.OriginalAnimationSrtPath = OriginalAnimationSrtPath;
    SubstitutionRequest.DonorAnimationPath = DonorAnimationPath;
    SubstitutionRequest.Label = Label;
    SubstitutionRequest.SubIndex = SubIndex;

    // Validate Input Parameters
    if (!ValidateInputParameters(SubstitutionRequest))
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
    SubstitutionContext.TargetSkeleton = SkeletalSequenceDataAsset->Skeleton.LoadSynchronous();
    SubstitutionContext.TargetSkeletalMesh = SkeletalSequenceDataAsset->SkeletalMesh.LoadSynchronous();
    if (!SubstitutionContext.TargetSkeleton || !SubstitutionContext.TargetSkeletalMesh)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to load target skeleton or skeletal mesh from SkeletalSequenceDataAsset."));
        return false;
    }
    
    // Subsystem
    SubstitutionContext.MocapSubsystem = GEditor->GetEditorSubsystem<UMocapImportSubsystem>();
    if (!SubstitutionContext.MocapSubsystem)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Could not obtain UMocapImportSubsystem!"));
        return false;
    }
    
    // Get the MovieScene and Rates
    SubstitutionContext.MovieScene = LevelSequence->GetMovieScene();
    if (!SubstitutionContext.MovieScene)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("LevelSequence does not have a valid MovieScene."));
        return false;
    }
    SubstitutionContext.DisplayRate = SubstitutionContext.MovieScene->GetDisplayRate();
    SubstitutionContext.TickResolution = SubstitutionContext.MovieScene->GetTickResolution();

    if (!LoadAnimSequence(SubstitutionContext.MocapSubsystem, SubstitutionRequest.OriginalAnimationPath, SubstitutionContext.TargetSkeleton, SubstitutionContext.OriginalAnim))
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to import original animation: %s"), *SubstitutionRequest.OriginalAnimationPath);
        return false;
    }

    if (!LoadAnimSequence(SubstitutionContext.MocapSubsystem, SubstitutionRequest.DonorAnimationPath, SubstitutionContext.TargetSkeleton, SubstitutionContext.DonorAnim))
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to import donor animation: %s"), *SubstitutionRequest.DonorAnimationPath);
        return false;
    }

    // Get the binding guid for the skeletal mesh track
    TArray<UMovieSceneSkeletalAnimationTrack*> OutTracks;
    FindAllTracks(SubstitutionContext.MovieScene, OutTracks);
    if (OutTracks.Num() > 0)
    {
        SubstitutionContext.MovieScene->FindTrackBinding(*OutTracks[0], SubstitutionContext.SkeletalMeshBindingId);
    }

    // Save the track
    SubstitutionContext.Track = OutTracks[0];

    if (!SubstitutionContext.SkeletalMeshBindingId.IsValid())
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to find a valid binding for the skeletal animation track."));
        return false;
    }

    // Reset the level sequence to remove all existing tracks and markers
    bool bResetSuccess = ResetLevelSequence(LevelSequence, SubstitutionContext.MovieScene, FGuid());
    if (!bResetSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to reset the level sequence."));
        return false;
    }

    return true;
}

// Stage 3: Remove Section and Add Donor Animation
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
    // Get the start frame of the section in the subindex from the marker section map
    FSectionLabelEntry* FoundEntry = MarkerSectionMap.Find(SectionIndex);
    
    if (!FoundEntry)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("No entry found for SectionIndex %d in MarkerSectionMap."), SectionIndex);
        return false;
    }

    // Print the found entry for debugging
    FString SectionName = FoundEntry->Section ? FoundEntry->Section->GetName() : TEXT("nullptr"); 
    
    // Get the start frame of the old section
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

    // Log the details of the new section for debugging
    int32 NewSectionStartFrame = NewAnimSection->GetRange().GetLowerBoundValue().Value;
    int32 NewSectionEndFrame = NewAnimSection->GetRange().GetUpperBoundValue().Value;
    // UE_LOG(LogBlendingAuto, Display, TEXT("New animation section added: Start Frame = %d, End Frame = %d, Label = %s"), NewSectionStartFrame, NewSectionEndFrame, *Label);

    // Edit the marker section map to point to the new section
    FoundEntry->Section = NewAnimSection;
    FoundEntry->Label = Label;

    // Mark sequence dirty so the modification can be saved
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
    const FSubstitutionRequest& SubstitutionRequest)
{
    if (!SubstitutionRequest.LevelSequence)                  { UE_LOG(LogBlendingAuto, Error, TEXT("LevelSequence is null.")); return false; }
    if (SubstitutionRequest.OriginalAnimationPath.IsEmpty())   { UE_LOG(LogBlendingAuto, Error, TEXT("OriginalAnimationPath is empty.")); return false; }
    if (SubstitutionRequest.OriginalAnimationSrtPath.IsEmpty()){ UE_LOG(LogBlendingAuto, Error, TEXT("OriginalAnimationSrtPath is empty.")); return false; }
    if (SubstitutionRequest.DonorAnimationPath.IsEmpty())      { UE_LOG(LogBlendingAuto, Error, TEXT("DonorAnimationPath is empty.")); return false; }
    if (SubstitutionRequest.Label.IsEmpty())                  { UE_LOG(LogBlendingAuto, Error, TEXT("Label is empty.")); return false; }
    if (SubstitutionRequest.SubIndex < 0)                     { UE_LOG(LogBlendingAuto, Error, TEXT("SubIndex is negative.")); return false; }

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


