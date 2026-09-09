#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSequence.h"
#include "Animation/AnimSequence.h"
#include "BlendingAutomationBFL.generated.h"

UCLASS()
class BLENDINGAUTOMATIONEDITOR_API UBlendingAutomationBFL : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    static bool ProcessAnimationSubstitution(
        ULevelSequence* LevelSequence,
        const FString& OriginalAnimationPath,
        const FString& OriginalAnimationSrtPath,
        const FString& DonorAnimationPath,
        const FString& Label,
        int32 SubIndex,
        UAnimSequence*& OutNewAnimation
    );
};