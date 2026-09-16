#include "BlendingAutomationBFL.h"
#include "MovieScene.h"
#include "Animation/Skeleton.h"
#include "Editor.h"

#include "VTTParser.h"
#include "SequencerAbstractionBPLibrary.h"
#include "MocapImportSubsystem.h"

bool UBlendingAutomationBFL::ProcessAnimationSubstitution(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& Label,
    int32 SubIndex,
    UAnimSequence*& OutNewAnimation)
{

    // 1. Validate Input Parameters
    if (!ValidateInputParameters(LevelSequence, OriginalAnimationPath, OriginalAnimationSrtPath, DonorAnimationPath, Label, SubIndex))
    {
        return false;
    }

    TArray<FVTTEntry> OutMarkers;
    bool bSuccess = UVTTParser::ParseVTTFile(OriginalAnimationSrtPath, OutMarkers);

    if (!bSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to parse VTT file."));
    }
    
    // get display rate from the level sequence
    UMovieScene* MovieScene = LevelSequence->GetMovieScene();
    if (!MovieScene)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to get MovieScene from LevelSequence."));
        return false;
    }
    
    FFrameRate DisplayRate = MovieScene->GetDisplayRate();
    UE_LOG(LogTemp, Display, TEXT("   - Display Rate: %f"), DisplayRate.AsDecimal());
    
    TArray<FMovieSceneMarkedFrame> PlacedMarkers;
    for (FVTTEntry& Marker : OutMarkers)
    {
        UVTTParser::UpdateVTTEntryFrames(Marker, DisplayRate); 

        // Add markers to the level sequence
        TArray<FMovieSceneMarkedFrame> NewMarkers = UVTTParser::AddVTTEntryMarkers(LevelSequence, Marker.StartFrame.Value, Marker.EndFrame.Value, Marker.Text);
        PlacedMarkers.Append(NewMarkers);
    }
    
    UE_LOG(LogTemp, Display, TEXT("   - Total Markers Placed: %d"), PlacedMarkers.Num());
    // show the markers in the level sequence
    UE_LOG(LogTemp, Display, TEXT("   - Markers in Level Sequence:"));
    for (const FMovieSceneMarkedFrame& Marker : PlacedMarkers)
    {
        UE_LOG(LogTemp, Display, TEXT("     - Marker: %s at frame %d"), *Marker.Label, Marker.FrameNumber.Value);
    }

    TArray<UMovieSceneSkeletalAnimationSection*> AnimSections;

    bool bFoundSections = FindAllSections(MovieScene, AnimSections);

    if (!bFoundSections || AnimSections.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No UMovieSceneSkeletalAnimationSection found in the Level Sequence!"));
        return false;
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


    // cut the animation into segments based on the markers

    bool bSplitSuccess = SplitAnimationSections(AnimSections[0], MovieScene, PlacedMarkers);

  

    // substitute a segment of the original animation with a segment from the donor animation

    // move the segments to blend together 

    // bake new animation into new animation fbx

    //reset the level sequence to use the new animation
    // out path to the new animation fbx
    FString outFirstImportedAssetPath;

    // Load the target skeleton asset first
    const FString SkeletonPath = TEXT("/Game/Avatars/CC/Palmer.Palmer"); // Or the exact skeleton asset path
    USkeleton* TargetSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);

    if (!TargetSkeleton)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load Skeleton at path: %s"), *SkeletonPath);
        return false;
    }

    UMocapImportSubsystem* MocapSubsystem = GEditor ? GEditor->GetEditorSubsystem<UMocapImportSubsystem>() : nullptr;

    if (!MocapSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("Could not obtain UMocapImportSubsystem!"));
        return false;
    }

    bool importSuccess = MocapSubsystem->importAnimationFromFbx(
        OriginalAnimationPath,
        TEXT("/Game/Animations/UnrealHeadless"),
        TargetSkeleton, // Pass the USkeleton* pointer
        true,
        outFirstImportedAssetPath
    );
    
    UE_LOG(LogTemp, Display, TEXT("Import Success: %s"), importSuccess ? TEXT("true") : TEXT("false"));
    UE_LOG(LogTemp, Display, TEXT("Imported Asset Path: %s"), *outFirstImportedAssetPath);

    return true;
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

bool UBlendingAutomationBFL::SplitAnimationSections(UMovieSceneSkeletalAnimationSection* Section, UMovieScene* MovieScene,const TArray<FMovieSceneMarkedFrame>& Markers) {
    UMovieSceneSkeletalAnimationSection* RightSplitSection = Section;
    UMovieSceneSkeletalAnimationSection* OutRightSplitSection = nullptr;
    UMovieSceneSkeletalAnimationSection* OutLeftSplitSection = nullptr;

    FFrameRate TickResolution = MovieScene->GetTickResolution();
    FFrameRate DisplayRate = MovieScene->GetDisplayRate();

    for (const FMovieSceneMarkedFrame& Marker : Markers)
    {
        FFrameTime DisplayTime = FFrameRate::TransformTime(
            FFrameTime(Marker.FrameNumber), 
            TickResolution, 
            DisplayRate
        );

        int32 DisplayFrame = DisplayTime.FloorToFrame().Value;

        UE_LOG(LogTemp, Display, TEXT("Splitting at marker: %s at display frame: %d, display rate: %d"), *Marker.Label, DisplayFrame, DisplayRate.Numerator);
        if (DisplayFrame <= RightSplitSection->GetRange().GetLowerBoundValue().Value || DisplayFrame >= RightSplitSection->GetRange().GetUpperBoundValue().Value)
        {
            UE_LOG(LogTemp, Warning, TEXT("Marker frame %d is outside the animation section range (%d to %d). Skipping split."), 
                DisplayFrame, 
                RightSplitSection->GetRange().GetLowerBoundValue().Value, 
                RightSplitSection->GetRange().GetUpperBoundValue().Value);
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

        if (OutLeftSplitSection && OutRightSplitSection)
        {
            UE_LOG(LogTemp, Display, TEXT("Successfully split the animation section at marker: %s"), *Marker.Label);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to split the animation section at marker: %s"), *Marker.Label);
        }

        RightSplitSection = OutRightSplitSection; // Continue splitting the right section for subsequent markers

    }
    return true;
}
