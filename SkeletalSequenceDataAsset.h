#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Misc/FrameRate.h"
#include "SkeletalSequenceDataAsset.generated.h"

class USkeleton;
class UAnimSequence;

USTRUCT(BlueprintType)
struct FFrameRateConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    bool bUseCustomFrameRate = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    FFrameRate TargetFrameRate = FFrameRate(30, 1);
};

UCLASS(BlueprintType)
class BLENDINGAUTOMATIONEDITOR_API USkeletalSequenceDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Direct reference to the Skeleton asset */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation Setup")
    TSoftObjectPtr<USkeleton> Skeleton;

    /** Direct reference to the Animation Sequence asset */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation Setup")
    TSoftObjectPtr<ULevelSequence> AnimationSequence;

    /** Configuration tuple containing a boolean flag and an FFrameRate */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation Setup")
    FFrameRateConfig FrameRateSettings;
};