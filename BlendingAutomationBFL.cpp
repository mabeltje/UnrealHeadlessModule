/**
 * BlendingAutomationBFL.cpp
 *
 * Manages the 5-stage animation substitution and blending automation pipeline:
 *   1. Load & Validate       - Validate inputs, load config/FBX assets, and reset the sequence.
 *   2. Place Markers & Split - Parse SRT/VTT markers and split the base animation into segments.
 *   3. Replace Section       - Remove the target segment, shift tail sections, and insert donor anim.
 *   4. Blend & Match Bone    - Overlap transitions and align pelvis transforms across cuts.
 *   5. Bake & Export         - Bake the blended sequence to a UAnimSequence asset and export FBX.
 */

#include "BlendingAutomationBFL.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Editor.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"

#include "AnimationExport.h"
#include "MocapImportSubsystem.h"
#include "SequencerAbstractionBPLibrary.h"
#include "SkeletalSequenceDataAsset.h"
#include "Subsystems/BlendingAutomationBlender.h"
#include "Subsystems/BlendingAutomationExporter.h"
#include "Subsystems/BlendingAutomationSplitter.h"

DEFINE_LOG_CATEGORY(LogBlendingAuto);

// ============================================================================
// Main Pipeline Function and Entry Point to the Unreal Process
// =============================================================================
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
    OutNewAnimation = nullptr;
    
    // Create the substitution context and request structures
    FSubstitutionContext SubstitutionContext;
    FSubstitutionRequest SubstitutionRequest;
    
    // --- Stage 1: Load and Validate ---
    UE_LOG(LogBlendingAuto, Display, TEXT("     [1/5] Loading & Validating assets..."));
    if (!LoadAndValidate(LevelSequence, OriginalAnimationPath, OriginalAnimationSrtPath, 
        DonorAnimationPath, OutputAnimationPath, Label, SubIndex, 
        SubstitutionContext, SubstitutionRequest))
    {
        return false;
    }

    // --- Stage 2: Place Markers and Split Sections ---
    UE_LOG(LogBlendingAuto, Display, TEXT("     [2/5] Parsing markers & splitting timeline..."));
    if (!FBlendingAutomationSplitter::PlaceMarkersAndSplit(SubstitutionContext, SubstitutionRequest))
    {
        return false;
    }

    // --- Stage 3: Remove Section and Add Donor Animation ---
    UE_LOG(LogBlendingAuto, Display, TEXT("     [3/5] Replacing section with donor animation..."));
    if (!ReplaceSection(SubstitutionRequest, SubstitutionContext))
    {
        return false;
    }

    // --- Stage 4: Blend Sections and Match to Bone ---
    UE_LOG(LogBlendingAuto, Display, TEXT("     [4/5] Blending overlaps & matching pelvis bone transforms..."));
    if (!FBlendingAutomationBlender::BlendAnimationSectionsAndMatchToBone(SubstitutionContext, SubstitutionRequest))
    {
        return false;
    }

    // --- Stage 5: Bake and Save ---
    UE_LOG(LogBlendingAuto, Display, TEXT("     [5/5] Baking final sequence to asset..."));
    if (!FBlendingAutomationExporter::BakeAnimationSequence(SubstitutionContext, SubstitutionRequest))
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to bake animation sequence."));
        return false;
    }

    // Load the baked animation sequence asset
    // TODO: Do not hardcode the path, make it a parameter or generate it based on the original animation sequence
    FString BakedAssetPath = FString::Printf(TEXT("/Game/Animations/BlendingAutomation/%s_Baked.%s_Baked"), *SubstitutionRequest.Label, *SubstitutionRequest.Label);
    OutNewAnimation = LoadObject<UAnimSequence>(nullptr, *BakedAssetPath);
    
    USequencerAbstractionBPLibrary::SaveAsset(LevelSequence);

    // Export final baked animation to FBX
    FString CleanExportDir = FPaths::ConvertRelativePathToFull(OutputAnimationPath);
    FString ExportFileNameBase = FPaths::GetBaseFilename(OutputAnimationPath);
    FString ExportFileName = FString::Printf(TEXT("%s_%s"), *ExportFileNameBase, *Label);

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

    UE_LOG(LogBlendingAuto, Display, TEXT("[Substitution Done] Successfully completed substitution for '%s'.\n"), *SubstitutionRequest.Label);

    return (OutNewAnimation != nullptr);
}


// =============================================================================
// Pipeline Stages
// =============================================================================

// Stage 1: Load and Validate
bool UBlendingAutomationBFL::LoadAndValidate(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& OutputAnimationPath,
    const FString& Label,
    int32 SubIndex,
    FSubstitutionContext& SubstitutionContext,
    FSubstitutionRequest& SubstitutionRequest
)
{  
    // Populate the SubstitutionRequest struct
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

    // Open the Level Sequence in the Sequencer to ensure it's ready for editing
    USequencerAbstractionBPLibrary::OpenLevelSequenceInSequencer(SubstitutionRequest.LevelSequence);

    // Load the Config
    // TODO: Do not hardcode the path, make it a parameter or generate it based on the original animation sequence
    USkeletalSequenceDataAsset* SkeletalSequenceDataAsset = LoadObject<USkeletalSequenceDataAsset>(nullptr, TEXT("/Game/Config/AutomationConfig.AutomationConfig"));
    if (!SkeletalSequenceDataAsset)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to load SkeletalSequenceDataAsset."));
        return false;
    }

    // Load the target skeleton from the SkeletalSequenceDataAsset
    SubstitutionContext.TargetSkeleton = SkeletalSequenceDataAsset->Skeleton.LoadSynchronous();
    SubstitutionContext.TargetSkeletalMesh = SkeletalSequenceDataAsset->SkeletalMesh.LoadSynchronous();
    SubstitutionContext.ControlRigClass = SkeletalSequenceDataAsset->ControlRigClass.LoadSynchronous();
    if (!SubstitutionContext.TargetSkeleton || !SubstitutionContext.TargetSkeletalMesh || !SubstitutionContext.ControlRigClass)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to load target skeleton or skeletal mesh from SkeletalSequenceDataAsset."));
        return false;
    }
    
    // Acquire editor import subsystem
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

    // Load the original and donor animation sequences
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

    // Store the output animation path in the substitution request for later use
    SubstitutionRequest.OutputAnimationPath = OutputAnimationPath;

    // Get the binding guid for the skeletal mesh track
    TArray<UMovieSceneSkeletalAnimationTrack*> OutTracks;
    FindAllTracks(SubstitutionContext.MovieScene, OutTracks);
    if (OutTracks.Num() > 0)
    {
        SubstitutionContext.MovieScene->FindTrackBinding(*OutTracks[0], SubstitutionContext.SkeletalMeshBindingId);
    } else
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("No skeletal animation tracks found in the MovieScene."));
        return false;
    }

    // Save the track
    SubstitutionContext.Track = OutTracks[0];

    if (!SubstitutionContext.SkeletalMeshBindingId.IsValid())
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to find a valid binding for the skeletal animation track."));
        return false;
    }

    // Clear existing sections and markers from the level sequence to prepare for the substitution process
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
    const FSubstitutionRequest& SubstitutionRequest,
    FSubstitutionContext& SubstitutionContext)
{   
    // The timeline is always split into 3 segments [Head (0), Target (1), Tail (2)]
    // TODO: Do not hardcode this, it works for now, because we only sub 1 section at a time
    // but if we want to sub multiple sections, we need to find the section index based
    int32 SectionIndex = 1;

    if (!RemoveSectionFromLevelSequence(SubstitutionRequest.LevelSequence, SubstitutionContext.MovieScene, SectionIndex, SubstitutionContext.MarkerSectionMap, SubstitutionContext.DisplayRate))
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to remove section from level sequence."));
        return false;
    }

    // Adjust the tail sections to fit the new section
    // TODO: Do not hardcode this, it works for now, because we only sub 1 section at a time
    // but if we want to sub multiple sections, we need to find the section index based
    // there is also no guarantee that the new section will always fit
    if (!FitTailSections(SubstitutionRequest.LevelSequence, SubstitutionContext.MovieScene, SubstitutionContext.DisplayRate, SubstitutionContext.MarkerSectionMap, SectionIndex, SubstitutionContext.DonorAnim))
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move tail sections to fit the new section."));
        return false;
    }

    // Add the donor animation section to the level sequence
    if (!AddDonorAnimationSection(SubstitutionRequest.LevelSequence, SubstitutionContext.MovieScene, SubstitutionContext.DisplayRate, SubstitutionContext.MarkerSectionMap, SubstitutionContext.Track, SubstitutionContext.DonorAnim, SectionIndex, SubstitutionRequest.Label))
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to add donor animation section to the level sequence."));
        return false;
    }

    return true;
}


// =============================================================================
// Validation & Asset Loading Helpers
// =============================================================================

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


// =============================================================================
// Sequencer Track & Section Manipulation Helpers
// =============================================================================

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


bool UBlendingAutomationBFL::FitTailSections(ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FFrameRate DisplayRate,
        TMap<int32, FSectionLabelEntry>& MarkerSectionMap,
        int32 SectionIndex,
        UAnimSequence* donorAnimSequence)
{   
    // TODO: Get this functions on the substitution context and request

    // Find the head and the tail sections
    FSectionLabelEntry* NewEntry = MarkerSectionMap.Find(SectionIndex);
    FSectionLabelEntry* TailEntry = MarkerSectionMap.Find(SectionIndex + 1);

    // Check if the entries exist
    if (!NewEntry || !TailEntry)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entries for SectionIndex %d or %d in MarkerSectionMap."), SectionIndex, SectionIndex + 1);
        return false;
    }

    // Get length of the donor animation sequence in frames
    FFrameRate TickResolution = MovieScene->GetTickResolution();
    FFrameTime DonorLength = donorAnimSequence->GetPlayLength() * TickResolution;

    if (!NewEntry || !TailEntry)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Could not find entries for SectionIndex %d or %d in MarkerSectionMap."), SectionIndex, SectionIndex + 1);
        return false;
    }

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


    // If the gap that was left is smaller than the donor animation length, move the tail section to the right by the difference
    int32 Difference = LengthBetweenSections - DonorLength.FloorToFrame().Value;

    // Move the tail section to fit the new donor animation length
    int32 NewStartFrameForTail = TailStartTime.FloorToFrame().Value - Difference;

    // Convert to the display rate
    FFrameTime NewStartDisplayTimeForTail = FFrameRate::TransformTime(FFrameTime(NewStartFrameForTail), MovieScene->GetTickResolution(), DisplayRate);

    // TODO: Find out if this is engough for all cases
    // Maybe floor to frame is not enough, maybe we need to ceil to frame or round to frame, or maybe we need to add 1 frame to the new start frame for the tail section
    // We need to make sure there is enough space for the new section to be added
    int32 NewStartDisplayFrameForTail = NewStartDisplayTimeForTail.FloorToFrame().Value + 1;

    FSequenceOpenResult MoveResult;
    bool bMoveSuccess = USequencerAbstractionBPLibrary::MoveAnimationSectionStartTo(LevelSequence, TailEntry->Section, NewStartDisplayFrameForTail, MoveResult);
    if (!bMoveSuccess)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("Failed to move section %s to start frame %d: %s"), *TailEntry->Section->GetName(), NewStartDisplayFrameForTail, *MoveResult.Error);
        return false;
    }

    return true;
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
    // TODO: Get this functions on the substitution context and request

    // Get the start frame of the section in the subindex from the marker section map
    FSectionLabelEntry* FoundEntry = MarkerSectionMap.Find(SectionIndex);
    
    if (!FoundEntry)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("No entry found for SectionIndex %d in MarkerSectionMap."), SectionIndex);
        return false;
    }

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


// =============================================================================
// Debugging Utilities
// =============================================================================

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

