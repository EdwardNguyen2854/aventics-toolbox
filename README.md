# Aventics Toolbox v0.4.1

Native Creo Parametric 9 Pro/TOOLKIT engineering toolbox with a five-tab Creo GUI.

v0.4.1 is a runtime hotfix for the v0.4.0 UI redesign. It corrects malformed native grid definitions that could prevent the toolbox dialog from loading and makes the dialog-open failure popup independent of `aventics_messages.txt`.

## Included tools

### Overview

- Opens all four functional tools directly: Weak Dimensions, Accuracy, Inspection, and Instance Builder.
- **Run All QC on Creo Selection** selects parts/assemblies once; Accuracy runs on both and Weak Dimensions runs on parts.
- Separates read-only quality control from assembly-modifying tools.

### Weak Dimensions

Sources:

- Creo selection: parts only.
- Folder: all `.prt` / `.prt.N` files.

Folder options:

- optional subfolders,
- latest Creo version only by default.

The checker is read-only. It tests every section dimension using a fresh `ProFeatureSectionCopy()` and calls `ProSecdimStrengthen()` only on that temporary copy.

v0.4.0 keeps the result table focused on part, status, feature, section, and weak dimension; the full path and diagnostic text are shown in the selected-result area.

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

v0.4.0 uses the same source/action/result hierarchy as Weak Dimensions and keeps long details outside the main table.

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
- rows can advance along X,
- placement can use the X-Y or X-Z plane,
- **does not save the assembly automatically**.

v0.4.0 splits discovery, source types, family-table options, placement direction/plane, and grid controls into lower-density rows. The X-Z choice is retained for the current Creo session and is read by the placement engine when an Inspection run starts.

### Instance Builder

Instance Builder accepts requested instance/code numbers and resolves exact standalone models or family-table instances from a source folder.

Workflow:

1. **Input** — choose the search folder and paste requested codes.
2. **Plan** — choose columns/gap and preview deterministic row/column allocation.
3. **Build and review** — search source models, add resolved models unconstrained to the active assembly, review unresolved codes, and optionally export CSV.

Input separators:

- one code per line,
- comma,
- semicolon,
- tab.

Behavior:

- preserves input order and duplicate requested codes,
- allocates cells row-major before source search,
- retrieves exact matching family-table instances only,
- keeps missing codes in their planned cells so later components do not shift,
- searches through the existing cancellable operation runner,
- performs assembly only after search completes, so cancelling during search adds no new components,
- adds components without constraints,
- does not save automatically,
- exports the final report as UTF-8 CSV.

v0.4.0 adds input-aware Plan/Build enablement, stale-plan guidance, a narrower review table, full selected-result details, and an **Unresolved only** filter.

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
     -> Instance Builder
```

Folder discovery, model loading, session preservation, family-table handling, STEP import, progress, cancellation, and shared result helpers remain native C++/Pro TOOLKIT services rather than a web UI layer.

The v0.4.0 interface keeps the v0.3.0 choose/configure/run/review foundation and further reduces visual density:

- five-tool Overview navigation,
- consistent Source / action / Results hierarchy,
- scannable result tables with full detail below the table,
- lower-density Inspection placement controls,
- staged Instance Builder workflow,
- global operation ownership in the footer,
- session retention for the Inspection X-Z choice and Instance Builder review filter.

v0.4.1 keeps that layout and fixes native resource structure so every grid row declares the same number of columns as controls. It also uses a direct Creo UI error dialog if the toolbox cannot be created, so error reporting does not depend on a message-file lookup.

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

`build-local.ps1` performs:

```text
CMake configure
-> Release build
-> verify aventics_toolbox.dll exists
-> protk_unlock
```

If you use a different Creo installation:

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

Expected development DLL for the documented release tree:

```text
C:\local\dev\AventicsToolbox_v0.4.1\dist\x86e_win64\obj\aventics_toolbox.dll
```

Then register:

```text
C:\local\dev\AventicsToolbox_v0.4.1\protk.dat
```

in:

```text
Creo -> Tools -> Auxiliary Applications
```

The command is designated as `AVT.OpenToolbox` and can be added through Creo's **TOOLKIT Commands** in Customize Ribbon.

## Native resource paths

The GUI resource is intentionally duplicated in both locations:

```text
text\resource\aventics_toolbox.res
text\usascii\resource\aventics_toolbox.res
```

`package-release.ps1` compares their SHA-256 hashes and refuses to package the release if they differ.

Message file:

```text
text\aventics_messages.txt
```

The regular menu/ribbon labels still use the message file. The toolbox-open failure path in v0.4.1 does not; it uses `ProUIMessageDialogDisplay()` directly and includes the TOOLKIT error name.

## Development cycle

Every DLL rebuild produces a new locked binary, so the normal cycle is:

```text
edit
-> .\build-local.ps1
-> Stop / Start AventicsToolbox in Auxiliary Applications
-> test
```

`build-local.ps1` runs `protk_unlock` after a successful build.

When changing `aventics_messages.txt`, fully restart Creo before testing the message change because Creo loads a message file only once per session.

## Folder version behavior

Given:

```text
body.prt.1
body.prt.3
body.prt.7
```

with **Latest version only** enabled, only:

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

Inspection and Instance Builder are different: models used by newly created assembly components must remain available in session, so they do not perform the QC cleanup behavior after placement.

## Cancellation

Long folder operations use a Creo UI timer and process one source model per timer tick. Cancel takes effect between models.

For Inspection, Cancel stops future additions; components already added to the active assembly are intentionally kept. No automatic rollback or save is performed.

For Instance Builder, source search completes before assembly begins. Cancelling during search therefore adds no new components.

## Logs

Development/runtime log:

```text
%LOCALAPPDATA%\Aventics\AventicsToolbox\logs\aventics_toolbox.log
```

If the native toolbox dialog cannot be created, v0.4.1 also shows the returned TOOLKIT error name in a direct Creo error popup.

## Build notes

The project uses:

- C++17,
- Visual Studio 2022 x64,
- `protk_dll_NU.lib`,
- `ucore.lib`,
- `udata.lib`.

The CMake runtime/library choices mirror the v0.1.0 setup that compiled successfully against the original target workstation. If another corporate Creo installation uses PTC's MD library variant, adapt the runtime and Toolkit library to match that installation's sample makefile.

## Release packaging

After final build + unlock:

```powershell
.\package-release.ps1
```

This produces:

```text
release\AventicsToolbox_v0.4.1\
release\AventicsToolbox_v0.4.1_release.zip
```

The package contains only the DLL, resources, version information, and installer scripts.

Team install target defaults to:

```text
%LOCALAPPDATA%\Aventics\AventicsToolbox
```

No Visual Studio/CMake/source is required on end-user machines.

## Known v0.4.1 limitations

- Folder paths passed to legacy Creo `ProPath` APIs are limited by the Creo TOOLKIT `ProPath` size.
- STEP import depends on the installed Creo import capability/license.
- STEP import uses Creo's normal import behavior/profile state; no custom STEP import profile UI is included.
- Inspection does not explode a STEP assembly into separate top-level inspection components; it inserts the imported top assembly once.
- Inspection has no one-click rollback.
- Instance Builder CSV is a report only; it does not persist run history inside the application.
- No automatic QC fixes.
- No automatic model or assembly save.
- Native Creo rendering, keyboard order, and Windows scaling still require in-Creo acceptance testing for each packaged build.

## Project layout

```text
AventicsToolbox_v0.4.1
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
│     ├─ inspection/
│     └─ instance_builder/
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
