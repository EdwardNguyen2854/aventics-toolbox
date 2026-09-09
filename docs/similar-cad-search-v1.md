# Similar CAD Search v1 — implementation notes

Branch: `feature/similar-cad-search-v1`

Tracking issue: #15

## Purpose

The first working slice proves the complete image-to-Creo retrieval workflow before adding a heavyweight learned embedding dependency.

```text
Creo part library
  -> discover .prt / .prt.N files
  -> load one part at a time through Pro/TOOLKIT
  -> generate 8 deterministic raster views
  -> compute a local visual signature for each view
  -> persist an incremental local index

query image
  -> compute the same visual signature
  -> compare against every indexed view
  -> best view score becomes the part score
  -> return top-K Creo files
```

The current scorer is intentionally labelled `prototype-signature-v1`. It is not a probability and it is not yet a learned CLIP/SigLIP-style embedding. The index has explicit schema/signature versions so the scorer can be replaced later without changing the Creo-facing workflow.

## Current v1 slice

### Input

- PNG
- JPG / JPEG
- BMP

### Creo library

- `.prt`
- versioned `.prt.N`
- optional subfolder traversal
- optional latest-version-only filtering

Assemblies and STEP files are intentionally not indexed in this slice.

### Standard views

Each indexed part gets 8 cached JPEG views:

1. default/front
2. back
3. left
4. right
5. top
6. bottom
7. isometric A
8. isometric B

The renderer uses Creo view reset/rotate/refit operations and `ProRasterFileWrite`. Existing displayed model views are restored after rendering when a model was already open.

### Local cache

Cache root:

```text
%LOCALAPPDATA%\AventicsToolbox\similar-cad\<library-hash>\
```

Contents:

```text
index.bin
renders\
  <model-hash>_v0.jpg
  <model-hash>_v1.jpg
  ...
```

`index.bin` stores:

- schema version
- signature version and length
- library path
- model path/name
- Creo file version
- file modification stamp
- cached render paths
- per-view signature vectors

A refresh reuses a model record when the source timestamp is unchanged and all 8 cached views/signatures are still valid. Removed source models disappear from the next saved index.

## Session safety

Indexing follows the same model-lifecycle principle as folder QC:

```text
capture session
-> load one part
-> render/index it
-> restore the previous view/window where possible
-> erase models introduced by that retrieval
-> continue
```

Models that were already present in the Creo session are not intentionally erased.

The index operation runs one source model per Creo UI timer tick, so Cancel takes effect between models. Cancelling keeps the previously saved index instead of replacing it with a partial index.

## Electron integration

Similar CAD Search uses a dedicated named pipe:

```text
\\.\pipe\aventics-similar-cad-<Creo PID>
```

This is separate from the existing toolbox QC/Builder pipe. The separation keeps search/index protocol changes from destabilizing existing tools.

Protocol actions currently include:

```text
ready
refresh
index <folder> <recursive> <latest>
cancel
search <folder> <query-image> <top-K>
open <part-path>
```

Windows file/folder pickers and Locate-in-Explorer are handled by the Electron main process.

## UI

The tool is exposed in:

- the sidebar
- a Control Panel card

The page contains:

- query image preview and picker
- Creo library folder and options
- Build / refresh index
- index statistics
- progress with rebuilt / unchanged / failed counts
- Cancel
- configurable top-K search
- ranked thumbnail results
- Open in Creo
- Locate in Explorer

The displayed similarity value is explicitly a visual similarity score, not a calibrated engineering probability.

## Why the prototype scorer exists

A learned vision embedding is still the intended direction, but the highest-risk integration questions come first:

- Can Creo batch-render thousands of models reliably?
- Are the standardized views useful and stable?
- Can models be loaded/cleaned without harming the working session?
- Is the local index/update workflow usable?
- Are image-to-CAD results useful enough to justify a larger runtime/model dependency?

The deterministic signature lets those questions be tested immediately with no cloud service and no ML runtime installation.

## Next technical milestone

Benchmark this branch with roughly 100–500 representative parts and a query set containing:

- an indexed render from a different view
- a Creo screenshot
- a clean catalog/product image
- family variants
- deliberately unrelated parts

Track at least:

- Top-1 hit rate
- Top-5 hit rate
- Top-20 hit rate
- indexing failures
- average indexing time per model
- incremental refresh time

Then compare `prototype-signature-v1` against a local ONNX vision encoder. The learned encoder should replace the prototype scorer only if retrieval quality materially improves enough to justify the model/runtime footprint.

## Remaining before calling v1 production-ready

- Run a real Creo 9/Creo 11 compile and smoke test on the target workstation.
- Validate view/raster behavior for already-open models and parts loaded only for indexing.
- Confirm datum/spin-center/display settings are consistently excluded or normalize them during rendering.
- Add a small repeatable retrieval benchmark.
- Decide whether `prototype-signature-v1` is retained as a fallback or replaced by an ONNX embedding provider.
- Tune cache cleanup for orphaned render files.
- Add release/version notes only after the branch passes workstation validation.
