#include "BlendingAutomationExporter.h"
#include "../BlendingAutomationBFL.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "SequencerAbstractionBPLibrary.h"

/**
 * class: FBlendingAutomationExporter
 * BlendingAutomationExporter.cpp
 * 
 * Bakes the blended animation sequences to a new animation sequence.
 * Work in progress: Exporting the markers to an SRT file does not work yet.
 */


bool FBlendingAutomationExporter::BakeAnimationSequence(
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

// Helper: Converts FFrameTime to standard SRT timestamp format (HH:MM:SS,mmm)
static FString FrameTimeToSrtTimestamp(FFrameTime FrameTime, FFrameRate FrameRate)
{
    double TotalSeconds = FrameRate.AsSeconds(FrameTime);
    if (TotalSeconds < 0.0)
    {
        TotalSeconds = 0.0;
    }

    int32 Hours = FMath::FloorToInt(TotalSeconds / 3600.0);
    int32 Minutes = FMath::FloorToInt(FMath::Fmod(TotalSeconds, 3600.0) / 60.0);
    int32 Seconds = FMath::FloorToInt(FMath::Fmod(TotalSeconds, 60.0));
    int32 Milliseconds = FMath::FloorToInt(FMath::Fmod(TotalSeconds, 1.0) * 1000.0);

    return FString::Printf(TEXT("%02d:%02d:%02d,%03d"), Hours, Minutes, Seconds, Milliseconds);
}

bool FBlendingAutomationExporter::ExportMarkersToSrt(ULevelSequence* LevelSequence, 
    const FString& OutputSrtFilePath)
{
    if (!LevelSequence)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("ExportMarkersToSrt: LevelSequence is null."));
        return false;
    }

    UMovieScene* MovieScene = LevelSequence->GetMovieScene();
    if (!MovieScene)
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("ExportMarkersToSrt: MovieScene is null."));
        return false;
    }

    const TArray<FMovieSceneMarkedFrame>& MarkedFrames = MovieScene->GetMarkedFrames();
    if (MarkedFrames.Num() == 0)
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("ExportMarkersToSrt: No marked frames found in Level Sequence."));
        return false;
    }

    FFrameRate TickResolution = MovieScene->GetTickResolution();
    FString SrtContent = TEXT("");
    int32 SrtIndex = 1;

    // Pair s: (start) and e: (end) markers together
    for (int32 i = 0; i < MarkedFrames.Num(); ++i)
    {
        const FMovieSceneMarkedFrame& StartMarker = MarkedFrames[i];
        
        // Process only Start markers (e.g., "s:GLOSS_LABEL")
        if (StartMarker.Label.StartsWith(TEXT("s:")))
        {
            FString GlossLabel = StartMarker.Label.RightChop(2); // Strip "s:" prefix
            FFrameTime StartFrameTime = FFrameTime(StartMarker.FrameNumber);
            FFrameTime EndFrameTime = StartFrameTime;
            bool bFoundMatchingEnd = false;

            // Search for the matching End marker ("e:GLOSS_LABEL")
            for (int32 j = i + 1; j < MarkedFrames.Num(); ++j)
            {
                const FMovieSceneMarkedFrame& EndMarker = MarkedFrames[j];
                if (EndMarker.Label.StartsWith(TEXT("e:")) && EndMarker.Label.RightChop(2) == GlossLabel)
                {
                    EndFrameTime = FFrameTime(EndMarker.FrameNumber);
                    bFoundMatchingEnd = true;
                    break;
                }
            }

            // Fallback: If no matching e: marker is found, use the next available marker as the end time
            if (!bFoundMatchingEnd && (i + 1 < MarkedFrames.Num()))
            {
                EndFrameTime = FFrameTime(MarkedFrames[i + 1].FrameNumber);
            }

            FString StartTimeStr = FrameTimeToSrtTimestamp(StartFrameTime, TickResolution);
            FString EndTimeStr = FrameTimeToSrtTimestamp(EndFrameTime, TickResolution);

            // Format standard SRT block:
            // 1
            // 00:00:01,000 --> 00:00:02,500
            // GLOSS_LABEL
            //
            SrtContent += FString::Printf(TEXT("%d\n%s --> %s\n%s\n\n"), SrtIndex, *StartTimeStr, *EndTimeStr, *GlossLabel);
            SrtIndex++;
        }
    }

    if (SrtContent.IsEmpty())
    {
        UE_LOG(LogBlendingAuto, Warning, TEXT("ExportMarkersToSrt: No valid start/end marker pairs found to export."));
        return false;
    }

    // Ensure target directory exists and save the file
    FString CleanPath = FPaths::ConvertRelativePathToFull(OutputSrtFilePath);
    FPaths::MakePlatformFilename(CleanPath);

    bool bSaved = FFileHelper::SaveStringToFile(SrtContent, *CleanPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (bSaved)
    {
        UE_LOG(LogBlendingAuto, Display, TEXT("Successfully exported SRT file to: %s"), *CleanPath);
    }
    else
    {
        UE_LOG(LogBlendingAuto, Error, TEXT("Failed to save SRT file to: %s"), *CleanPath);
    }

    return bSaved;
}
