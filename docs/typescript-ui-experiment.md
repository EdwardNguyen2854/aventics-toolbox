# TypeScript / Electron UI experiment

Branch: `experiment/typescript-webview-ui`

This branch keeps the Creo TOOLKIT backend in the native C++ DLL and runs the TypeScript toolbox UI in a separate Electron process. WebView2 is no longer used.

## Architecture

```text
Creo Parametric
  -> aventics_toolbox.dll (C++ / Creo TOOLKIT)
      -> ElectronToolbox.cpp
          -> hidden Win32 message window (Creo-thread dispatch + operation timer)
          -> Windows named-pipe server: \\.\pipe\aventics-toolbox-<CreoPID>
      -> existing tools/common C++ backend

Electron
  -> electron/main.ts (window + named-pipe client)
  -> electron/preload.ts (context-isolated bridge)
  -> src/app.ts (existing TypeScript renderer)
```

The Electron renderer never calls Creo directly. All model loading, selection, checking, family-table work, placement, and assembly modification stays in native TOOLKIT code.

The named-pipe reader runs on a worker thread, but every command that can touch Creo is posted to a hidden Win32 message window and therefore handled on the Creo thread that opened the toolbox command.

## What is migrated

- Control Panel
- Weak Dimensions UI and result table
- Accuracy UI and result table
- Inspection Builder setup, placement options, progress, and results
- Instance Builder multiline input, planning, build, unresolved filter, CSV export, and results
- Full-screen/maximize action
- Shared progress/status bar and cancellation
- Native Creo selection and folder picker still run in C++
- Multiple Creo sessions are isolated by PID-specific named pipes
- Existing native Creo dialog remains the fallback if Electron cannot launch

## Prerequisites

The normal Creo TOOLKIT build prerequisites still apply. The Electron experiment additionally needs:

1. Node.js LTS (`npm` on PATH).
2. Access to an npm registry that can provide the `electron` and `typescript` packages.

There is no WebView2 SDK, NuGet package, `WEBVIEW2_SDK_DIR`, or `WebView2Loader.dll` requirement.

## Build

The normal local command builds/stages Electron first, then builds and unlocks the native DLL:

```powershell
.\build-local.ps1
```

To build only the native DLL while working on backend code:

```powershell
.\build-local.ps1 -SkipUi
```

To build/stage only Electron:

```powershell
.\build-ui.ps1
```

The first Electron build runs `npm install` if `ui\node_modules\electron\dist\electron.exe` is not present. If your corporate environment uses an internal npm registry or proxy, configure npm before running the build.

The staged runtime layout is:

```text
dist/x86e_win64/obj/
  aventics_toolbox.dll
  electron/
    electron.exe
    resources/
    locales/
    ... Chromium/Electron runtime files ...
    app/
      package.json
      index.html
      electron-shim.js
      styles.css
      dist/app.js
      dist-electron/main.js
      dist-electron/preload.js
```

## UI-only development

You can preview the UI without Creo:

```powershell
cd ui
npm run dev
```

With no `--pipe` argument Electron starts in mock mode and renders a fake active assembly. This is intentionally lightweight; richer mock datasets can be added later.

## Runtime behavior

When the Creo command is clicked:

1. The DLL creates a hidden message window and a named pipe such as `\\.\pipe\aventics-toolbox-23840`.
2. If Electron is already connected, the DLL sends a focus command and the latest state.
3. Otherwise the DLL launches the staged Electron runtime and passes `--pipe` and `--creo-pid`.
4. Electron connects and sends `ready`.
5. The DLL sends the current model/session/tool state as JSON.
6. UI actions travel Electron IPC -> named pipe -> hidden Creo-thread message window -> existing C++ backend.

State messages are newline-delimited JSON. The existing renderer command protocol is temporarily retained behind a small compatibility shim so the UI screens did not need a second rewrite during the transport migration.

## Test checklist

1. Run `npm run dev` under `ui` and confirm the Electron window opens in mock mode.
2. Run `.\build-local.ps1` and confirm no WebView2/NuGet setup is requested.
3. Start Creo and load the toolbox registration as usual.
4. Open **Aventics Toolbox**. Confirm an Electron window opens and displays the current Creo model.
5. Click the Creo command a second time and confirm the existing Electron window is focused instead of a duplicate being launched.
6. Test Weak Dimensions with Creo selection and folder mode.
7. Test Accuracy with parts and assemblies.
8. Open a disposable assembly and test Inspection Builder. Confirm the assembly is not auto-saved.
9. Test Instance Builder with more than two pasted codes. Confirm missing instances leave their planned position empty and later requests keep their original row/column.
10. Test Cancel during a multi-model run.
11. Close Electron and reopen it from the Creo command; confirm it reconnects and restores C++ state.
12. Close Creo while Electron is open; confirm the Electron process closes from the native shutdown control (or at minimum reports the disconnected session).
13. Open two Creo sessions and confirm each launches an Electron window connected to its own PID-specific pipe.

## Current experiment boundaries

- The existing TypeScript renderer still uses the small WebView-style `postMessage` surface internally through `electron-shim.js`; the actual transport is Electron IPC/named pipes. This can be replaced with a typed `window.aventics` API after runtime validation.
- The native Creo dialog remains compiled as a fallback and behavior reference.
- Electron is staged from `node_modules` rather than packaged into a branded single executable yet.
- The branch still requires compile/runtime verification on the target Windows/Creo workstation because the repository depends on PTC TOOLKIT libraries that are unavailable in generic CI environments.
