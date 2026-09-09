#include "BlendingAutomationBFL.h"

#include "VTTParser.h"

bool UBlendingAutomationBFL::ProcessAnimationSubstitution(
    ULevelSequence* LevelSequence,
    const FString& OriginalAnimationPath,
    const FString& OriginalAnimationSrtPath,
    const FString& DonorAnimationPath,
    const FString& Label,
    int32 SubIndex,
    UAnimSequence*& OutNewAnimation)
{
    // Test print all parameters to verify Python called it correctly
    UE_LOG(LogTemp, Display, TEXT("[BlendingAutomation C++] ProcessAnimationSubstitution called successfully!"));
    UE_LOG(LogTemp, Display, TEXT("  - LevelSequence: %s"), LevelSequence ? *LevelSequence->GetName() : TEXT("nullptr"));
    UE_LOG(LogTemp, Display, TEXT("  - Original FBX:  %s"), *OriginalAnimationPath);
    UE_LOG(LogTemp, Display, TEXT("  - Original SRT:  %s"), *OriginalAnimationSrtPath);
    UE_LOG(LogTemp, Display, TEXT("  - Donor FBX:     %s"), *DonorAnimationPath);
    UE_LOG(LogTemp, Display, TEXT("  - Label:         %s"), *Label);
    UE_LOG(LogTemp, Display, TEXT("  - SubIndex:      %d"), SubIndex);

    OutNewAnimation = nullptr; // placeholder until you return a generated animation

    outMarkers = UVTTParser::ParseVTTFile(OriginalAnimationSrtPath);

    UE_LOG(LogTemp, Display, TEXT("  - Parsed Markers: %d"), outMarkers.Num());

    return true;
}