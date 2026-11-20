#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Misc/FrameRate.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalSequenceDataAsset.generated.h"

class USkeleton;
class UAnimSequence;
class ULevelSequence;

// USTRUCT(BlueprintType)
// struct FBlendingPipelineSettings
// {
//     GENERATED_BODY()

//     /** Target segment index when timeline is split into [Head, Target, Tail]. */
//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending")
//     int32 DefaultTargetSectionIndex = 1;

//     /** Percentage of donor section length used to overlap adjacent sections. */
//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending", meta = (ClampMin = "0", ClampMax = "100"))
//     int32 OverlapPercentage = 20;

//     /** Offset in display frames applied between section cuts during blending/tail fitting. */
//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending")
//     int32 DisplayFrameOffset = 10;

//     /** Bone used to align transforms across section boundaries. */
//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending")
//     FName MatchBoneName = TEXT("pelvis");
// };

// USTRUCT(BlueprintType)
// struct FPipelinePathAndExportSettings
// {
//     GENERATED_BODY()

//     /** Content Browser directory used for temporary FBX imports and baked AnimSequences. */
//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paths & Export", meta = (ContentDir))
//     FDirectoryPath WorkingAnimationDirectory = { TEXT("/Game/Animations/BlendingAutomation") };

//     /** Suffix appended to the label when naming baked UAnimSequence assets. */
//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paths & Export")
//     FString BakedAssetSuffix = TEXT("_Baked");

//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paths & Export")
//     bool bExportBinaryFbx = false;

//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paths & Export")
//     bool bForceFrontXAxis = false;

//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paths & Export")
//     bool bExportPreviewMesh = false;
// };

UCLASS(BlueprintType)
class BLENDINGAUTOMATIONEDITOR_API USkeletalSequenceDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Direct reference to the Skeleton asset */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation Setup")
    TSoftObjectPtr<USkeleton> Skeleton;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation Setup")
    TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

    // These are not used in the current pipeline, but could be useful for future extensions
//     /** Direct reference to the Animation Sequence asset */
//     UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation Setup")
//     TSoftObjectPtr<ULevelSequence> AnimationSequence;

//     /** Configuration tuple containing a boolean flag and an FFrameRate */
//     UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation Setup")
//     FFrameRateConfig FrameRateSettings;

    // UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pipeline Configuration")
    // FBlendingPipelineSettings BlendingSettings;

    // UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pipeline Configuration")
    // FPipelinePathAndExportSettings ExportSettings;
};

// USTRUCT(BlueprintType)
// struct FFrameRateConfig
// {
//     GENERATED_BODY()

//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
//     bool bUseCustomFrameRate = false;

//     UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
//     FFrameRate TargetFrameRate = FFrameRate(30, 1);
// };