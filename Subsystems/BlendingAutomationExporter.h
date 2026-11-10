#pragma once

#include "CoreMinimal.h"
#include "BlendingAutomationTypes.h"

class FBlendingAutomationExporter
{
public:
    static bool BakeAnimationSequence(
        ULevelSequence* LevelSequence,
        UMovieScene* MovieScene,
        FGuid SkeletalMeshBindingId,
        const FString& Label
    );
 
private:

};