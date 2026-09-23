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

    // Cut the animation into segments based on the markers
    TMap<int32, FSectionLabelEntry> MarkerSectionMap;
    bool bSplitSuccess = SplitAnimationSections(NewSection, MovieScene, PlacedMarkers, MarkerSectionMap);
    
    if (!bSplitSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to split animation sections based on markers."));
        return false;
    }

    PrintSections(MovieScene);

    // Print map of marker labels to their corresponding sections
    UE_LOG(LogTemp, Display, TEXT("Marker to Section Mapping:"));
    for (const TPair<int32, FSectionLabelEntry>& Pair : MarkerSectionMap)
    {
        if (Pair.Value.Section)
        {
            UE_LOG(LogTemp, Display, TEXT("   - Marker [%d] %s -> Section: %s"), 
                Pair.Key, 
                *Pair.Value.Label, 
                *Pair.Value.Section->GetName());
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("   - Marker [%d] %s -> Section: nullptr"), 
                Pair.Key, 
                *Pair.Value.Label);
        }
    }

    // Remove a section from the level sequence
    bool bRemoveSuccess = RemoveSectionFromLevelSequence(LevelSequence, MovieScene, SubIndex, MarkerSectionMap, DisplayRate);

    if (!bRemoveSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to remove section from level sequence."));
        return false;
    }

    PrintSections(MovieScene);

    // Add the donor animation section to the level sequence
    bool AddSuccess = AddDonorAnimationSection(Track, donorAnimSequence);

    // move the segments to blend together 

    // bake new animation into new animation fbx

    //reset the level sequence to use the new animation
    // out path to the new animation fbx
    
    // Save the level sequence after modifications
    USequencerAbstractionBPLibrary::SaveAsset(LevelSequence);

    return true;
}

bool UBlendingAutomationBFL::AddDonorAnimationSection(
    UMovieSceneSkeletalAnimationTrack* Track, 
    UAnimSequence* donorAnimSequence
    ) 
{   
    // 1. Add section to track & cast
    UMovieSceneSection* RawSection = Track->CreateNewSection();
    Track->AddSection(*RawSection);

    UMovieSceneSkeletalAnimationSection* NewSection = Cast<UMovieSceneSkeletalAnimationSection>(RawSection);
    if (!NewSection)
    {
        UE_LOG(LogTemp, Error, TEXT("Cast to MovieSceneSkeletalAnimationSection Failed Trying To Replace Animation"));
        return false;
    }

    NewSection->Params.Animation = donorAnimSequence;

    // int32 LengthNewSection = donorAnimSequence->GetNumberOfSampledKeys();
    // int32 EndFrameNewSection = StartFrameOldSection + LengthNewSection;

    UE_LOG(LogTemp, Display, TEXT("Added Donor Animation Section: %s"), *NewSection->GetName());
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
        TMap<int32, FSectionLabelEntry> MarkerSectionMap,
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
    
    UE_LOG(LogTemp, Display, TEXT("Removing section for SubIndex %d: Label: %s, Section: %s"), 
        SubIndex, 
        *FoundEntry->Label, 
        FoundEntry->Section ? *FoundEntry->Section->GetName() : TEXT("nullptr"));

        
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

    UE_LOG(LogTemp, Display, TEXT("Section to remove: %s, End Frame (Display Rate): %d"), *SectionToRemove->GetName(), SectionEndDisplayFrame);

    FSequenceOpenResult RemoveResult;
    bool bRemoveSuccess = USectionAbstraction::RemoveAnimationSection(LevelSequence, SectionToRemove, RemoveResult);

    if (!bRemoveSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to remove animation section: %s"), *RemoveResult.Error);
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
        UE_LOG(LogTemp, Warning, TEXT("Failed to parse VTT file."));
    }
    
    for (FVTTEntry& Marker : OutMarkers)
    {
        UVTTParser::UpdateVTTEntryFrames(Marker, DisplayRate); 

        // Add markers to the level sequence
        TArray<FMovieSceneMarkedFrame> NewMarkers = UVTTParser::AddVTTEntryMarkers(LevelSequence, Marker.StartFrame.Value, Marker.EndFrame.Value, Marker.Text);
        PlacedMarkers.Append(NewMarkers);
    }
    
    UE_LOG(LogTemp, Display, TEXT("   - Total Markers Placed: %d"), PlacedMarkers.Num());
    UE_LOG(LogTemp, Display, TEXT("   - Markers in Level Sequence:"));
    for (const FMovieSceneMarkedFrame& Marker : PlacedMarkers)
    {
        UE_LOG(LogTemp, Display, TEXT("     - Marker: %s at frame %d"), *Marker.Label, Marker.FrameNumber.Value);
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
    
    UE_LOG(LogTemp, Display, TEXT("Import Success: %s"), importSuccess ? TEXT("true") : TEXT("false"));
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
            UE_LOG(LogTemp, Warning, TEXT("Marker %s is outside the bounds of the section. Skipping split."), *Marker.Label);
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

        MarkerSectionMap.Add(MarkerIndex, USectionAbstraction::CreateSectionLabelEntry(OutLeftSplitSection, Marker.Label) );
        MarkerIndex++;
    }

    // Print the final sections after all splits with their names and display ranges
    TArray<UMovieSceneSkeletalAnimationSection*> FinalSections;
    FindAllSections(MovieScene, FinalSections);
    UE_LOG(LogTemp, Display, TEXT("Final sections after all splits:"));
    for (UMovieSceneSkeletalAnimationSection* SplitSection : FinalSections)
    {
        FFrameTime LowerBoundDisplayTime = FFrameRate::TransformTime(
            FFrameTime(SplitSection->GetRange().GetLowerBoundValue().Value), 
            TickResolution, 
            DisplayRate
        );
        FFrameTime UpperBoundDisplayTime = FFrameRate::TransformTime(
            FFrameTime(SplitSection->GetRange().GetUpperBoundValue().Value), 
            TickResolution, 
            DisplayRate
        );
        
        UE_LOG(LogTemp, Display, TEXT(" - %s (Range: %d to %d)"), *SplitSection->GetName(), LowerBoundDisplayTime.FloorToFrame().Value, UpperBoundDisplayTime.FloorToFrame().Value);
    }

    return true;
}
