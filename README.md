# Aventics Toolbox v0.5.0

Native Creo Parametric 9 Pro/TOOLKIT engineering toolbox for Windows x64.

v0.5.0 replaces the previous five-tab workspace with a **Control Panel**. Each functional tool opens in its own native Creo GUI, and every tool GUI includes **Full screen**, **Control Panel**, cancel-operation, and close actions.

## Included tools

### Control Panel

The Control Panel is the main Aventics Toolbox GUI. From it you can open:

- Weak Dimensions,
- Accuracy,
- Inspection,
- Instance Builder.

It also keeps **Run all QC on Creo selection** for running Weak Dimensions and Accuracy from one model selection. Results remain in session and can be reviewed by opening either QC tool.

### Weak Dimensions

Sources:

- Creo selection: parts only,
- folder: `.prt` / `.prt.N` files.

Folder options include subfolders and latest-version-only filtering. The checker is read-only: it tests temporary section copies and does not strengthen dimensions in the original model.

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

The checker reads model accuracy only and does not modify or save the model.

### Inspection Assembly Builder

Folder sources:

- Creo parts (`.prt`, `.prt.N`),
- Creo assemblies (`.asm`, `.asm.N`),
- family-table instances,
- STEP (`.stp`, `.step`).

Behavior:

- requires an active Creo assembly,
- adds discovered top-level models without constraints,
- optionally uses family-table instances,
- imports STEP to temporary Creo models,
- supports auto-arranged or same-origin placement,
- supports row direction and X-Y / X-Z placement planes,
- **does not save the assembly automatically**.

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

v0.5.0 fixes large multiline paste handling in the requested-code field. The native text area now has 10 visible rows and a 32,767-character maximum length, and its enabled state is not redundantly reset on each text-input callback.

Instance allocation is fixed before source search. When a requested instance is not found, its planned grid position stays empty and the assembler continues with the next request at that request's own planned row/column. Later components therefore do not shift into the missing position.

Other behavior:

- preserves input order and duplicate requested codes,
- retrieves exact matching standalone/family-table instances only,
- searches through the cancellable operation runner,
- performs assembly only after source search completes,
- adds components without constraints,
- does not save automatically,
- exports the final report as UTF-8 CSV.

## v0.5.0 GUI architecture

```text
Creo 9
  -> protk.dat
  -> aventics_toolbox.dll
  -> Aventics Toolbox Control Panel
       -> Weak Dimensions GUI
       -> Accuracy GUI
       -> Inspection GUI
       -> Instance Builder GUI
```

The project is still one Pro/TOOLKIT DLL. Tool GUIs are separate native resources rather than separate processes or DLLs.

Tool state is retained through `AppContext` while navigating between the Control Panel and tool dialogs. Navigation is blocked while an operation is running so the active operation's UI is not destroyed.

The Full screen action expands the active tool dialog using native Creo TOOLKIT dialog-sizing APIs. The user can resize the dialog afterward to leave full-screen sizing.

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
C:\local\dev\AventicsToolbox_v0.5.0\dist\x86e_win64\obj\aventics_toolbox.dll
```

Register the generated `protk.dat` in:

```text
Creo -> Tools -> Auxiliary Applications
```

The command is `AVT.OpenToolbox` and can be added through Creo's **TOOLKIT Commands** in Customize Ribbon.

## Native resource paths

The v0.5.0 GUI resources are:

```text
text\resource\aventics_toolbox.res
text\resource\aventics_weak.res
text\resource\aventics_accuracy.res
text\resource\aventics_inspection.res
text\resource\aventics_instance_builder.res
```

Every file is mirrored under:

```text
text\usascii\resource\
```

`package-release.ps1` compares SHA-256 hashes for all five resource pairs and refuses to package the release if any pair differs.

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

`build-local.ps1` runs `protk_unlock` after a successful build.

When changing `aventics_messages.txt`, fully restart Creo before testing the message change because Creo loads a message file only once per session.

## Folder version behavior

Given:

```text
body.prt.1
body.prt.3
body.prt.7
```

with **Latest version only** enabled, only `body.prt.7` is processed. Plain unversioned `.prt` / `.asm` files are also recognized.

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

Inspection and Instance Builder differ: models used by newly created assembly components must remain available in session, so they do not perform the QC cleanup behavior after placement.

## Cancellation

Long folder operations use a Creo UI timer and process one source model per timer tick. Cancel takes effect between models.

For Inspection, Cancel stops future additions; components already added to the active assembly are intentionally kept. No automatic rollback or save is performed.

For Instance Builder, source search completes before assembly begins. Cancelling during search therefore adds no new components.

Tool switching is prevented while an operation is active.

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

The CMake runtime/library choices mirror the original target workstation. If another Creo installation uses PTC's MD library variant, adapt the runtime and Toolkit library to match that installation's sample makefile.

## Release packaging

After final build + unlock:

```powershell
.\package-release.ps1
```

This produces:

```text
release\AventicsToolbox_v0.5.0\
release\AventicsToolbox_v0.5.0_release.zip
```

The package contains the DLL, resources, version information, and installer scripts.

Team install target defaults to:

```text
%LOCALAPPDATA%\Aventics\AventicsToolbox
```

No Visual Studio/CMake/source is required on end-user machines.

## Known limitations

- Folder paths passed to legacy Creo `ProPath` APIs are limited by the Creo TOOLKIT `ProPath` size.
- STEP import depends on the installed Creo import capability/license.
- Inspection has no one-click rollback.
- Instance Builder CSV is a report only; it does not persist run history inside the application.
- No automatic QC fixes.
- No automatic model or assembly save.
- Native Creo rendering, keyboard order, dialog resource loading, and Windows scaling still require in-Creo acceptance testing for each packaged build.
