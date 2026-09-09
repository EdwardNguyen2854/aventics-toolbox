# Similar CAD Search v1 — implementation and validation notes

Branch: `feature/similar-cad-search-v1`

Tracking issue: #15

Draft PR: #17

## Current milestone

The original technical spike proved that Creo parts could be rendered, cached, indexed, and searched. Workstation testing then exposed three issues that are now the focus of this stabilization pass:

1. captured orientations were not reliably the intended views;
2. the Electron Toolbox could indirectly stall indexing until it was closed;
3. the index was not directly browseable/inspectable after creation.

The query scorer was also too sensitive to background, aspect distortion, and a single accidental best-view match.

This update changes both the capture path and the query path while preserving the existing local/offline architecture.

## Updated workflow

```text
Creo part library
  -> discover .prt / .prt.N files
  -> load one part
  -> orient one canonical view with an absolute normalized matrix
  -> yield to Creo
  -> raster that view
  -> yield to Creo
  -> analyze/cache that view
  -> yield to Creo
  -> repeat for 8 verified views
  -> restore the previous view/window
  -> clean models introduced by indexing
  -> persist a versioned local index

query image
  -> preserve source aspect ratio
  -> estimate background / isolate strongest foreground component
  -> crop with margin when Auto isolate is enabled
  -> letterbox and center without stretching
  -> build separate silhouette and edge descriptors
  -> compare with all indexed views
  -> combine shape + edge + proportion similarity
  -> aggregate the strongest 3 view scores
  -> return top-K Creo parts
```

## Capture profile v2

The current profile is deliberately still **8 views** until workstation inspection confirms the orientations are correct:

1. `FRONT`
2. `BACK`
3. `RIGHT`
4. `LEFT`
5. `TOP`
6. `BOTTOM`
7. `ISO_NE`
8. `ISO_NW`

The previous reset/rotate sequence has been replaced by normalized absolute `ProViewMatrixSet` transforms. The `FRONT`, `RIGHT`, and `TOP` bases are aligned with the normalized matrix convention used by PTC's `UgGraphViewsSave` sample: identity for FRONT, the sample SIDE basis for RIGHT, and the sample TOP basis for TOP. Opposite views flip the corresponding axis. The active capture profile is reported as `ptc-axis-8-v2`.

Each view is oriented and repainted in one timer step, then rastered in a later timer step. This gives Creo time to process its window/message loop before capture.

The index schema/signature version is now `2`. Existing v1 indexes intentionally require a rebuild because the capture labels, descriptor data, and query engine changed.

## Responsiveness and Toolbox independence

The first spike performed all views for one model inside a single timer callback and wrote UI state synchronously through the named pipe. On a real workstation this could leave Creo appearing not responsive and could allow a slow/stalled Electron consumer to influence capture progress.

The new index state machine is:

```text
Select source
  -> Load source
  -> Orient view
  -> Capture raster
  -> Analyze view
  -> Orient next view
  -> ...
  -> Finalize source
  -> next source
```

There is a short timer yield between each state.

State delivery to Electron now uses a dedicated writer thread with latest-state coalescing. Creo's main/UI thread only builds and queues state; it no longer waits for the Electron pipe write to complete.

Expected behavior:

- indexing continues if the Toolbox window is closed;
- reconnecting/reopening the Toolbox receives the latest host state;
- a slow UI cannot block the Creo capture loop;
- Cancel cleans the model currently being captured and keeps the previously saved index.

## Query engine v2

The active scorer is now labelled:

```text
hybrid-shape-v2
```

It is still local and deterministic; it is not a calibrated probability and it is not yet the learned ONNX encoder planned for the next retrieval milestone.

### What changed

The v1 query path resized the source into a fixed square before feature extraction. That distorted long/thin or tall parts and made background/lighting edges overly influential.

v2:

- decodes with the original aspect ratio preserved;
- estimates a background color from corner patches;
- uses adaptive color/edge thresholds;
- finds the strongest connected foreground component with a center prior;
- optionally crops around that component;
- fits it into a normalized square canvas without stretching;
- computes a 16x16 silhouette descriptor;
- computes a separate 16x16 intensity/silhouette-edge descriptor;
- stores object aspect ratio and normalized fill ratio.

The UI shows both the original query and the normalized query preview. Users can disable **Automatically isolate and center the main object** to compare against the full image when automatic foreground detection is wrong.

### Ranking

Each query/view comparison contains:

- silhouette/shape similarity;
- edge similarity;
- aspect/fill proportion similarity.

A per-view hybrid score is computed from those signals. The part score then aggregates the strongest three indexed views instead of letting one accidental view completely determine the result.

Search results expose:

- winning canonical view name;
- shape score;
- edge score;
- proportion score.

These fields are diagnostic signals, not probabilities.

## Indexed-library browser

Similar CAD Search now has two sections:

```text
Search | Library
```

The Library section works from the persisted index and provides:

- indexed-part count;
- server-side name/path filtering;
- pagination;
- cached thumbnail;
- Open in Creo;
- Locate in Explorer;
- Use as query;
- Inspect captured views.

`Inspect` displays every cached canonical view together with its view name and basic descriptor metrics. This is the primary capture-debugging surface: a bad orientation, clipped model, or inconsistent fit is visible before blaming the ranking model.

Protocol additions include:

```text
prepareQuery <image> <auto-crop>
search <folder> <image> <top-K> <auto-crop>
browse <folder> <filter> <page> <page-size>
inspect <part-path>
```

The Similar CAD protocol remains isolated on:

```text
\\.\pipe\aventics-similar-cad-<Creo PID>
```

## Local cache

Cache root:

```text
%LOCALAPPDATA%\AventicsToolbox\similar-cad\<library-hash>\
```

The v2 render files use named canonical views:

```text
renders\
  <model-hash>_FRONT.jpg
  <model-hash>_BACK.jpg
  <model-hash>_RIGHT.jpg
  ...
```

Normalized query previews are stored separately under:

```text
%LOCALAPPDATA%\AventicsToolbox\similar-cad\query-previews\
```

The persisted index stores:

- schema/signature versions;
- library path;
- model path/name;
- Creo file version;
- source modification stamp;
- canonical view name;
- cached render path;
- aspect/fill metrics;
- silhouette + edge descriptor.

A refresh reuses a model only when the source timestamp, canonical view set, descriptor version, and cached images are all still valid.

## Session safety

The indexing lifecycle remains:

```text
snapshot session
-> retrieve one part
-> capture/analyze views incrementally
-> restore previous view/window when applicable
-> erase models introduced by retrieval
-> continue
```

Models already present in the session are not intentionally erased.

## Validation checklist for this update

### Capture correctness

- [ ] `FRONT` is repeatable and visually the intended front orientation.
- [ ] `BACK`, `LEFT`, `RIGHT`, `TOP`, and `BOTTOM` are distinct and correct.
- [ ] both isometric views are distinct and useful.
- [ ] rerunning the same part produces the same orientations.
- [ ] model fit/crop is consistent enough for retrieval.
- [ ] already-open model view is restored after indexing.

### Responsiveness

- [ ] Creo remains responsive between raster operations.
- [ ] indexing continues with the Toolbox open.
- [ ] indexing continues after the Toolbox is closed.
- [ ] reopening the Toolbox reconnects and shows current/completed state.
- [ ] Cancel during a model cleans up correctly.

### Library browser

- [ ] indexed parts are searchable by name/path.
- [ ] pagination works for a library larger than one page.
- [ ] Inspect shows all 8 named cached views.
- [ ] Open and Locate work from the library and inspector.
- [ ] a cached view can be promoted to the query.

### Query quality

Build a repeatable query set containing:

- Creo screenshots;
- indexed renders from different orientations;
- clean catalog/product images;
- family variants;
- long/thin and tall parts;
- cluttered-background photos;
- unrelated parts.

Track at least:

- Top-1 hit rate;
- Top-5 hit rate;
- Top-20 hit rate;
- capture failures;
- average indexing time/model;
- incremental refresh time.

Compare Auto isolate ON/OFF for difficult real-world queries.

## Next retrieval milestone

Do **not** increase the view count until the 8-view inspector confirms the capture profile is correct on the target Creo workstation.

After that validation:

1. expand the verified canonical view set toward roughly 24 views;
2. benchmark `hybrid-shape-v2`;
3. add a local learned ONNX vision-embedding provider behind the same descriptor/index boundary;
4. retrieve broadly with the learned embedding and rerank the top candidates with the existing silhouette/edge/proportion signals;
5. retain the deterministic v2 descriptor as a diagnostic/fallback if useful.

The learned model asset/runtime is intentionally not bundled in this stabilization commit; capture correctness and Creo responsiveness must be verified first so model-quality testing is not contaminated by bad source views.
