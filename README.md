# BlendingAutomationEditor

An Unreal Engine editor module that automates **animation substitution and blending** in a Level Sequence. Given a base mocap animation, a subtitle file marking gloss boundaries, and a donor animation, it replaces one segment of the base animation with the donor, blends the transitions, matches the pelvis across the cuts, and bakes and exports the result.

## What it does

The pipeline is exposed as a single Blueprint-callable function, `UBlendingAutomationBFL::ProcessAnimationSubstitution`, and runs in five stages:

| Stage | Class / File | Description |
|---|---|---|
| 1. Load & Validate | `UBlendingAutomationBFL` | Validates inputs, loads the config data asset, imports the original and donor FBX files, finds the skeletal animation track and binding, and resets the Level Sequence (removes existing sections and markers). |
| 2. Place Markers & Split | `FBlendingAutomationSplitter` | Parses the SRT/VTT file into `s:` / `e:` markers on the timeline, adds the original animation, and splits it into `[Head, Target, Tail]` sections around `SubIndex`. |
| 3. Replace Section | `UBlendingAutomationBFL` | Removes the target section, shifts the tail section to fit the donor animation's length, and inserts the donor animation at the old section's start. |
| 4. Blend & Match Bone | `FBlendingAutomationBlender` | Offsets the donor and tail sections by a transition gap (10 display frames), then matches the `pelvis` bone transform at each section start to avoid root/hip pops. Finally fits the sequence playback range to the new timeline. |
| 5. Bake & Export | `FBlendingAutomationExporter` | Bakes the skeletal mesh binding to a new `UAnimSequence` and exports it as FBX via `UAnimationExport`. |

## Project layout

```
BlendingAutomationEditor/
├── BlendingAutomationBFL.h/.cpp        # Blueprint entry point + stages 1 and 3
├── BlendingAutomationEditor.Build.cs   # Module build rules
├── BlendingAutomationEditorModule.h/.cpp
├── SkeletalSequenceDataAsset.h         # Config data asset (skeleton + skeletal mesh)
└── Subsystems/
    ├── BlendingAutomationTypes.h       # FSubstitutionRequest / FSubstitutionContext
    ├── BlendingAutomationSplitter.h/.cpp   # Stage 2
    ├── BlendingAutomationBlender.h/.cpp    # Stage 4
    └── BlendingAutomationExporter.h/.cpp   # Stage 5
```

## Dependencies

Declared in `BlendingAutomationEditor.Build.cs`:

- Engine modules: `Core`, `CoreUObject`, `Engine`, `LevelSequence`, `MovieScene`, `MovieSceneTracks`, `MovieSceneTools`, `UnrealEd`
- Project/plugin modules: `SequencerAbstraction`, `UnrealVTTParser`, `AssetFileManagement`
- Private: `BlendingAutomation` (base module)

The code also relies on `UMocapImportSubsystem` (FBX import) and `UAnimationExport` (FBX export), provided by the modules above.

## Setup

1. Add the module to your project/plugin and make sure the dependent modules listed above are available.
2. Create a `SkeletalSequenceDataAsset` at **`/Game/Config/AutomationConfig`** and assign:
   - `Skeleton`: the target skeleton
   - `SkeletalMesh`: the target skeletal mesh (must be present in the editor world, since pelvis matching looks up a component using it)
3. Prepare a Level Sequence that already contains a **skeletal mesh binding with a skeletal animation track**.
4. Make sure the folder `/Game/Animations/BlendingAutomation` is usable for imported and baked assets.

## Usage

Call from Blueprint or C++:

```cpp
UAnimSequence* NewAnim = nullptr;
bool bOk = UBlendingAutomationBFL::ProcessAnimationSubstitution(
    LevelSequence,
    OriginalAnimationPath,     // base FBX
    OriginalAnimationSrtPath,  // SRT/VTT with gloss markers
    DonorAnimationPath,        // replacement FBX
    Label,                     // used to name the baked asset and FBX
    OutputAnimationPath,       // export directory / base name
    SubIndex,                  // zero-based gloss index to replace
    NewAnim);                  // [out] baked UAnimSequence
```

| Parameter | Description |
|---|---|
| `LevelSequence` | Target Level Sequence with a skeletal animation track. |
| `OriginalAnimationPath` | File path to the base FBX animation. |
| `OriginalAnimationSrtPath` | SRT/VTT file defining segment boundaries. |
| `DonorAnimationPath` | File path to the replacement FBX animation. |
| `Label` | Identifier for the baked asset (`<Label>_Baked`) and exported FBX. |
| `OutputAnimationPath` | Destination directory/base path for the exported FBX. |
| `SubIndex` | Zero-based index of the gloss segment to replace. |
| `OutNewAnimation` | Loaded baked `UAnimSequence`, or `nullptr` on failure. |

### Outputs

- A baked animation asset at `/Game/Animations/BlendingAutomation/<Label>_Baked`
- An FBX exported to `OutputAnimationPath` named `<OutputBaseName>_<Label>`
- The Level Sequence is saved

## Marker convention

Each gloss in the subtitle file becomes a pair of timeline markers: `s:<TEXT>` (start) and `e:<TEXT>` (end). `SubIndex` counts these pairs in chronological order.

The cut points depend on where the target gloss sits:

- **Middle gloss**: cut at its own start and end markers.
- **First gloss (`SubIndex == 0`)**: if there is a gap before the next gloss, the cut extends to the next gloss's start so the transition gap belongs to the target.
- **Last gloss**: if there is a gap after the previous gloss, the cut starts at the previous gloss's end.

The result is stored in `FSubstitutionContext::MarkerSectionMap` as `[Head (0), Target (1), Tail (2)]`.

## Key types

- `FSubstitutionRequest`: immutable inputs (paths, label, `SubIndex`, Level Sequence).
- `FSubstitutionContext`: mutable runtime state shared across stages (skeleton, mesh, MovieScene, binding ID, tick/display rates, imported anims, marker/section map).

## Known limitations / TODO

- The target section index is hardcoded to `1` (`[Head, Target, Tail]`), so only one segment can be substituted at a time.
- The transition gap is a fixed 10 display frames (`DisplayFrameOffset`) rather than calculated.
- The matched bone is hardcoded to `pelvis`.
- Paths are hardcoded: the config asset (`/Game/Config/AutomationConfig`), the working/baked directory (`/Game/Animations/BlendingAutomation`), and the baked asset name.
- `FitTailSections` uses `FloorToFrame() + 1`, which may not leave enough space in all cases.
- Marker-to-SRT export (`FBlendingAutomationExporter::ExportMarkersToSrt`) is a work in progress and is not yet called from the pipeline.
- The FBX export result is only logged; a failed export does not fail the pipeline.
- `SkeletalSequenceDataAsset.h` contains commented-out settings structs (blend settings, export settings) intended for future configuration.
- `BlendingAutomationExporter.h` documents `BakeAnimationSequence` with outdated parameter names.

## Logging

All output uses the `LogBlendingAuto` category. Filter the Output Log by it to follow each stage (`[1/5]` through `[5/5]`).