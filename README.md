# Aventics Toolbox v0.3.0

Native Creo Parametric 9 Pro/TOOLKIT engineering toolbox with a tabbed Creo GUI.

## Included tools

### Overview

- Open each tool tab directly.
- **Run All QC on Creo Selection**: select parts/assemblies once; Accuracy runs on both and Weak Dimensions runs on parts.

### Weak Dimensions

Sources:

- Creo selection: parts only.
- Folder: all `.prt` / `.prt.N` files.

Folder options:

- optional subfolders,
- latest Creo version only by default.

The checker is read-only. It tests every section dimension using a fresh `ProFeatureSectionCopy()` and calls `ProSecdimStrengthen()` only on that temporary copy.

### Accuracy

Sources:

- Creo selection,
- folder.

Supported models:

- parts,
- assemblies.

QC rule:

```text
Accuracy Type  = ABSOLUTE
Accuracy Value = 0.001
```

The checker only reads model accuracy and does not modify/save the model.

### Inspection Assembly Builder

Folder sources:

- Creo parts (`.prt`, `.prt.N`),
- Creo assemblies (`.asm`, `.asm.N`),
- family-table instances,
- STEP (`.stp`, `.step`).

Behavior:

- requires an active Creo assembly,
- adds each discovered top-level model to the active assembly,
- adds no assembly constraints,
- family-table instances can be added instead of the generic,
- STEP files are imported to temporary `AVT_STEP_####` Creo names,
- a STEP assembly is inserted once as its imported top-level assembly,
- default auto-layout uses model bounding boxes,
- disabling Auto arrange places models at the same origin,
- **does not save the assembly automatically**.

## UX / architecture

One application externally:

```text
Creo 9
  -> protk.dat
  -> aventics_toolbox.dll
  -> Aventics Toolbox native dialog
     -> Overview
     -> Weak Dimensions
     -> Accuracy
     -> Inspection
```

Folder discovery, model loading, session preservation, family-table handling, STEP import, progress and cancellation are shared common services rather than duplicated per tool.

The v0.3.0 interface adds a consistent choose/configure/run/review flow, selection-or-folder source modes for QC, searchable issue views, selected-result details, strict Inspection placement validation and operation feedback that remains visible while switching tabs.

## Target environment used for this project

```text
Creo:
C:\Program Files\PTC\Creo 9.0.2.0

Toolkit includes:
C:\Program Files\PTC\Creo 9.0.2.0\Common Files\protoolkit\includes

Toolkit libs:
C:\Program Files\PTC\Creo 9.0.2.0\Common Files\protoolkit\x86e_win64\obj

Unlock:
C:\Program Files\PTC\Creo 9.0.2.0\Parametric\bin\protk_unlock.bat
```

## Fastest local build

From PowerShell:

```powershell
cd D:\3-work\aventics-toolbox
.\build-local.ps1
```

`build-local.ps1` is preconfigured for the paths above and performs:

```text
CMake configure
-> Release build
-> verify aventics_toolbox.dll exists
-> protk_unlock
```

If you use a different Creo installation, use:

```powershell
.\build-and-unlock.ps1 `
  -ToolkitInclude "C:\path\to\protoolkit\includes" `
  -ToolkitLib "C:\path\to\protoolkit\x86e_win64\obj" `
  -CreoCommonLib "C:\path\containing\ucore.lib-and-udata.lib" `
  -UnlockBat "C:\path\to\Parametric\bin\protk_unlock.bat"
```

## Generate protk.dat

After a successful build:

```powershell
.\make-protk.ps1
```

Expected development DLL:

```text
C:\local\dev\AventicsToolbox_v0.3.0\dist\x86e_win64\obj\aventics_toolbox.dll
```

Then register:

```text
C:\local\dev\AventicsToolbox_v0.3.0\protk.dat
```

in:

```text
Creo -> Tools -> Auxiliary Applications
```

The command is also designated as `AVT.OpenToolbox` and can be added through Creo's **TOOLKIT Commands** in Customize Ribbon.

## Native resource paths

The GUI resource is intentionally duplicated in both locations:

```text
text\resource\aventics_toolbox.res
text\usascii\resource\aventics_toolbox.res
```

Message file:

```text
text\aventics_messages.txt
```

## Development cycle

Every DLL rebuild produces a new locked binary, so the normal cycle is:

```text
edit
-> .\build-local.ps1
-> Stop / Start AventicsToolbox in Auxiliary Applications
-> test
```

`build-local.ps1` already runs `protk_unlock` after a successful build.

## Folder version behavior

Given:

```text
body.prt.1
body.prt.3
body.prt.7
```

with **Latest Creo file version only** enabled, only:

```text
body.prt.7
```

is processed.

Plain unversioned `body.prt` / `module.asm` files are also recognized.

## Session safety

Folder QC does not intentionally leave hundreds of models in memory:

```text
snapshot session
-> retrieve one source model
-> run check
-> erase models introduced by that retrieval
-> next model
```

Models that already existed in the Creo session are preserved.

Inspection Builder is different: models used by newly created assembly components must remain available in session, so it does not perform the QC cleanup behavior.

## Cancellation

Long folder operations use a Creo UI timer and process one source model per timer tick. Cancel takes effect between models.

For Inspection Builder, Cancel stops future additions; components already added to the active assembly are intentionally kept. No automatic rollback or save is performed.

## Logs

Development/runtime log:

```text
%LOCALAPPDATA%\Aventics\AventicsToolbox\logs\aventics_toolbox.log
```

## Build notes

The project uses:

- C++17,
- Visual Studio 2022 x64,
- `protk_dll_NU.lib`,
- `ucore.lib`,
- `udata.lib`.

The CMake runtime/library choices mirror the v0.1.0 setup that compiled successfully against the target Creo 11 M050 installation. If another corporate Creo installation uses PTC's MD library variant, adapt the runtime and Toolkit library to match the sample makefile for that installation.

## Release packaging

After final build + unlock:

```powershell
.\package-release.ps1
```

This produces a user-facing release tree/ZIP under `release` containing only the DLL, resources, version information and installer scripts.

Team install target defaults to:

```text
%LOCALAPPDATA%\Aventics\AventicsToolbox
```

No Visual Studio/CMake/source is required on end-user machines.

## Known v0.3.0 limitations

- Folder paths passed to legacy Creo `ProPath` APIs are limited by the Creo TOOLKIT `ProPath` size.
- STEP import depends on the installed Creo import capability/license.
- STEP import uses Creo's normal import behavior/profile state; no custom STEP import profile UI is included yet.
- Inspection Builder does not explode a STEP assembly into separate top-level inspection components; it inserts the imported top assembly once.
- Inspection Builder has no one-click rollback in v0.3.0.
- No CSV/Excel report yet.
- No automatic QC fixes.
- No automatic model or inspection-assembly save.

## Project layout

```text
AventicsToolbox_v0.3.0
├─ include/
│  ├─ app/
│  ├─ common/
│  ├─ config/
│  └─ tools/
├─ src/
│  ├─ app/
│  ├─ common/
│  └─ tools/
│     ├─ weak_dimension/
│     ├─ accuracy/
│     └─ inspection/
├─ text/
│  ├─ resource/
│  ├─ usascii/resource/
│  └─ aventics_messages.txt
├─ installer/
├─ docs/
├─ dist/
├─ CMakeLists.txt
├─ build.ps1
├─ build-and-unlock.ps1
├─ build-local.ps1
├─ make-protk.ps1
├─ package-release.ps1
└─ VERSION.txt
```
