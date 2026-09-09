# TypeScript / WebView2 UI experiment

Base experiment branch: `experiment/typescript-webview-ui`

Hardened review branch: `fix/typescript-webview-review`

This branch keeps the Creo TOOLKIT backend in the existing C++ DLL and uses a TypeScript UI hosted in Microsoft Edge WebView2. The C++ layer remains the only layer that calls Creo. The hardened branch adds explicit WebView2 lifecycle, trust-boundary, command-dispatch, and build protections found during the comprehensive implementation review.

## Architecture

```text
Creo Parametric
  -> aventics_toolbox.dll (C++ / Creo TOOLKIT)
      -> WebToolbox.cpp (Win32 + WebView2 host)
          -> trusted local TypeScript UI
          -> deferred native command queue
      -> existing tools/common C++ backend
```

The TypeScript UI never calls Creo directly. It sends presentation commands to the C++ bridge; all model loading, selection, checking, family-table work, placement, and assembly modification stays in native TOOLKIT code.

## Hardening added on the review branch

- WebView messages no longer call `ProSelect`, file/folder dialogs, or other modal native work directly from a WebView2 event callback. Messages are parsed and posted to the Win32 host loop first.
- WebView startup has an explicit initialization state and a 10-second TypeScript `ready` handshake/watchdog.
- Synchronous and asynchronous WebView2 startup failures both fall back to the existing native Creo UI.
- Async environment/controller callbacks are scoped to a host generation so a close/reopen cannot attach an old callback to a new window.
- WebView2 Runtime availability is checked before asynchronous environment creation.
- Navigation outside the packaged `ui/index.html` document is blocked; new windows are blocked; web messages are accepted only from the expected local document.
- DevTools, default context menus, and the WebView status bar are disabled for this test build.
- Browser process failure triggers a deferred native-UI fallback.
- Operation timer creation failures abort cleanly and keep partial results rather than leaving the UI permanently busy.
- Large native operations use lightweight progress messages between batched full-result snapshots.
- The custom Win32 window class is unregistered during normal plugin shutdown.
- The TypeScript build now runs `tsc` with `noEmitOnError` instead of transpile-only compilation.
- TypeScript is pinned to an exact package version and the runtime UI assets are explicit CMake build dependencies, so HTML/CSS/JS edits redeploy without depending on a DLL relink.

## Hot unload policy

`allow_stop` is intentionally **FALSE** on this WebView2 test branch. Do not stop/unload the DLL from Creo and reload it in the same process. Restart Creo when testing a newly built DLL.

This removes the highest-risk DLL-lifetime path while WebView2 owns asynchronous callbacks. The host still cleans up its controller, environment, loader, timers, and registered Win32 class during normal application termination.

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
2. Microsoft WebView2 Runtime installed on the Creo workstation.
3. Network access to `api.nuget.org` on the first build, unless a WebView2 SDK package is already supplied locally.

The build pins `Microsoft.Web.WebView2` version `1.0.4191.47` and automatically downloads/extracts it into:

```text
external/Microsoft.Web.WebView2.1.0.4191.47/
```

`/external/` is git-ignored, so the SDK is cached locally and is not committed.

If the workstation cannot access NuGet, manually extract a `Microsoft.Web.WebView2` NuGet package and either set:

```powershell
$env:WEBVIEW2_SDK_DIR = "C:\path\to\Microsoft.Web.WebView2.<version>"
```

or pass `-WebView2Sdk` to `build.ps1` / `build-and-unlock.ps1`.

The package root must contain:

```text
build/native/include/WebView2.h
```

## Build

For the existing local Creo 9 build flow, no extra WebView2 SDK setup should normally be needed:

```powershell
.\build-local.ps1
```

On the first run, the script downloads the pinned WebView2 SDK. Later builds reuse the cached package under `external/`.

CMake type-checks/compiles the TypeScript UI and deploys these runtime files:

```text
dist/x86e_win64/obj/
  aventics_toolbox.dll
  WebView2Loader.dll
  ui/
    index.html
    styles.css
    app.js
```

The UI assets remain external during the experiment so they can be iterated quickly without embedding resources into the DLL.

## Test checklist

1. Build and unlock the DLL.
2. Start Creo and load the toolbox registration as usual.
3. Confirm the Auxiliary Application cannot be hot-stopped (`allow_stop FALSE`).
4. Open **Aventics Toolbox**. The window title should contain `TypeScript UI`.
5. Confirm the Control Panel shows the current Creo model name.
6. Test Weak Dimensions with Creo selection and folder mode.
7. Test Accuracy with parts and assemblies.
8. Open a disposable assembly and test Inspection Builder. Confirm the assembly is not auto-saved.
9. Test Instance Builder with more than two pasted codes. Confirm missing instances leave their planned position empty and later requests keep their original row/column.
10. Test Cancel during a multi-model run.
11. Close and reopen the toolbox several times in one Creo session and confirm only one WebView host exists each time.
12. Temporarily remove `WebView2Loader.dll` and confirm the native Creo UI opens synchronously.
13. Test on a machine without WebView2 Runtime if available and confirm the native fallback opens.
14. Temporarily break `ui/app.js` or the ready handshake and confirm the startup watchdog closes the web host and opens the native UI.
15. Attempt external navigation/new-window creation from a temporary test page and confirm it is blocked.
16. Restart Creo after rebuilding the DLL; do not use hot stop/reload on this branch.

## Current boundaries

- The host is still a normal Win32 top-level window inside the Creo process, not yet embedded in a Creo NakedWindow.
- The existing native Creo dialog remains compiled as fallback and as a behavior reference.
- UI assets and the WebView2 loader remain external beside the DLL during the experiment.
- Tool orchestration is still duplicated between the native dialog and WebView host. A future cleanup should extract a UI-independent controller shared by both front ends before this becomes the production default.
- The web UI does not yet restore every native result action such as Open Model / Goto Feature.
- Full-result snapshots are batched, but very large result sets may eventually warrant table virtualization and a delta-result protocol.
- This branch still requires compile/runtime verification on the target Windows/Creo workstation because CI here does not have the PTC TOOLKIT libraries.
