# TypeScript / WebView2 UI experiment

Branch: `experiment/typescript-webview-ui`

This branch keeps the Creo TOOLKIT backend in the existing C++ DLL and replaces the primary toolbox window with a TypeScript UI hosted in Microsoft Edge WebView2. If the web host cannot start synchronously (for example, the loader or UI assets are missing), the command falls back to the existing native Creo UI.

## Architecture

```text
Creo Parametric
  -> aventics_toolbox.dll (C++ / Creo TOOLKIT)
      -> WebToolbox.cpp (Win32 + WebView2 host and typed message bridge)
          -> ui/src/app.ts (TypeScript presentation)
      -> existing tools/common C++ backend
```

The TypeScript UI never calls Creo directly. It sends small commands to the C++ bridge; all model loading, selection, checking, family-table work, placement, and assembly modification stays in native TOOLKIT code.

## What is migrated

- Control Panel
- Weak Dimensions UI and result table
- Accuracy UI and result table
- Inspection Builder setup, placement options, progress, and results
- Instance Builder multiline input, planning, build, unresolved filter, CSV export, and results
- Full-screen/maximize action
- Shared progress/status bar and cancellation
- Native Creo selection and folder picker are still invoked by the C++ bridge when needed

Long-running source lists are processed one item per Win32 timer tick on the Creo UI thread so the WebView remains responsive while TOOLKIT calls stay on the same thread that opened the command.

## Prerequisites

The normal Creo TOOLKIT build prerequisites still apply. The experiment additionally needs:

1. Node.js LTS (`npm` on PATH).
2. Microsoft WebView2 Runtime installed on the Creo workstation. Current Windows/Edge installations normally include it, but verify it if the window fails to initialize.
3. The `Microsoft.Web.WebView2` NuGet package extracted locally so the native header and `WebView2Loader.dll` are available at build time.

Example with `nuget.exe`:

```powershell
mkdir external -ErrorAction SilentlyContinue
nuget install Microsoft.Web.WebView2 -OutputDirectory external
$pkg = Get-ChildItem .\external -Directory -Filter "Microsoft.Web.WebView2*" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
$env:WEBVIEW2_SDK_DIR = $pkg.FullName
```

Do not commit the extracted package; `/external/` is ignored.

## Build

For the existing local Creo 9 build flow:

```powershell
$env:WEBVIEW2_SDK_DIR = "C:\path\to\Microsoft.Web.WebView2.<version>"
.\build-local.ps1
```

Or pass the package root directly to `build.ps1` / `build-and-unlock.ps1` with `-WebView2Sdk`.

CMake builds `ui/src/app.ts` into `ui/dist/app.js`, then copies these runtime files beside the DLL:

```text
dist/x86e_win64/obj/
  aventics_toolbox.dll
  WebView2Loader.dll
  ui/
    index.html
    styles.css
    app.js
```

The UI assets are intentionally external during this experiment so they can be edited and rebuilt quickly without embedding resources into the DLL.

## Test checklist

1. Build and unlock the DLL.
2. Start Creo and load the toolbox registration as usual.
3. Open **Aventics Toolbox**. The window title should contain `TypeScript UI`.
4. Confirm the Control Panel shows the current Creo model name.
5. Test Weak Dimensions with Creo selection and folder mode.
6. Test Accuracy with parts and assemblies.
7. Open a disposable assembly and test Inspection Builder. Confirm the assembly is not auto-saved.
8. Test Instance Builder with more than two pasted codes. Confirm missing instances leave their planned position empty and later requests keep their original row/column.
9. Test Cancel during a multi-model run.
10. Close and reopen the toolbox and confirm the C++ `AppContext` values are restored into the web UI.

## Current experiment boundaries

- The host is a normal Win32 top-level window created inside the Creo process. It is not yet embedded in a Creo NakedWindow.
- The existing native Creo dialog remains compiled as a fallback and as a behavior reference during migration.
- UI assets and the WebView2 loader are deployed beside the DLL rather than embedded.
- This branch has not been validated on the target Creo workstation by CI; compile/runtime verification must be done on the Windows/Creo machine because the repository build depends on PTC TOOLKIT libraries.
