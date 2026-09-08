# Aventics Toolbox v0.4.0 release plan

Status: proposed. Prepared 2026-09-08 against `main` after v0.3.0 and the merged Instance Builder work.

## Release objective

Make Aventics Toolbox feel like one coherent engineering workspace rather than five function-complete tabs that happen to share a dialog.

v0.3.0 established consistent choose/configure/run/review flows, shared operation feedback, result search/filtering for QC, session retention, and resize behavior. v0.4.0 should build on that foundation by reducing visual density, clarifying hierarchy, making the new Instance Builder a first-class workflow, and making result review faster at common Creo screen sizes.

The target remains a native Creo Parametric dialog. Do not introduce a web renderer, custom theme engine, custom font dependency, or a new UI framework.

## Current evidence and design debt

This is still a source-based assessment. Native Creo screenshots and runtime validation are required before final dimensions are considered final.

| Current evidence | v0.4.0 response |
| --- | --- |
| The dialog now has five tabs, including Instance Builder, but Overview only links Weak Dimensions, Accuracy, and Inspection. | Make all tools discoverable from Overview and update product/navigation copy for the five-tool structure. |
| README and current v0.3.0 product description still document the older four-tab architecture. | Treat documentation and navigation consistency as release work, not cleanup after the release. |
| QC result tables include wide free-text `Details` columns while also showing selected-row details below the table. | Remove duplicated long-form content from the table where possible; keep tables scannable and move full technical context to a dedicated details area. |
| Inspection puts recursive/latest/type discovery controls in dense horizontal rows and placement controls in one long row. | Split controls by task and concept; shorten labels and keep related controls aligned in predictable rows. |
| Instance Builder combines target status, folder source, pasted codes, allocation controls, plan/build actions, progress, an eight-column table, details, export, and clear in one vertical flow. | Turn it into an explicit three-stage workflow: input, plan, build/review. Reduce table width and show state transitions clearly. |
| Footer status and per-tab progress text can carry overlapping long messages. | Define one global operation-status contract and keep local progress text short and tool-specific. |
| `InspUseZAxis` exists in the resource and `InspectionOptions` supports `useZAxisForRows`, but the current dialog state/options path does not appear to read or retain that control. | Treat orphaned or partially wired controls as P0 UX defects: wire them end-to-end or remove them from the UI before polishing layout. |
| The two resource files are intentionally mirrored manually. | Keep them byte-equivalent and add a release check that catches resource drift before packaging. |
| Native tables are rebuilt through common helpers and selected identity is already preserved in QC refresh paths. | Keep stable row identity and selection; extend the same interaction quality to Inspection and Instance Builder filters/review. |

Primary implementation areas are expected to be `text/resource/aventics_toolbox.res`, `text/usascii/resource/aventics_toolbox.res`, `src/app/ToolboxDialog.cpp`, `src/tools/instance_builder/InstanceBuilderUi.cpp`, `src/common/UiUtils.cpp`, `include/app/AppContext.h`, and user-facing docs.

## Product design principles

1. **One obvious next action.** Each tab should have one visually dominant action for its current state. Secondary actions should not compete with it.
2. **Progressive disclosure without hidden surprises.** Disable irrelevant controls when a mode does not use them; if native visibility changes are reliable, hide only controls whose disappearance does not shift the layout unpredictably.
3. **Results first after a run.** Once results exist, the main visual weight should move from setup to results and selected-item details.
4. **Short in the table, complete in details.** Tables should answer identity, status, and key technical facts. Full paths and long diagnostics belong in the details area or exported report.
5. **Shared patterns across tools.** Source, options, action, progress, results toolbar, table, details, and contextual actions should appear in the same order wherever the workflow allows it.
6. **Native and restrained.** Use Creo-native controls, sentence case, concise labels, consistent spacing, and text-based status. Do not depend on color alone.
7. **Safe by construction.** Read-only QC remains visibly distinct from assembly-modifying tools. Any action that adds components must show its active target before execution and continue to avoid automatic save.

## Information architecture

Keep five tabs:

1. Overview
2. Weak Dimensions
3. Accuracy
4. Inspection
5. Instance Builder

Do not add more top-level tabs in v0.4.0.

### Overview

Redesign Overview as a compact launch/status page for all tools.

Recommended grouping:

- **Quality control**
  - Run all QC on Creo selection
  - Weak Dimensions
  - Accuracy
- **Assembly tools**
  - Inspection
  - Instance Builder

Each tool entry should have:

- a short purpose line,
- a direct Open action,
- an optional one-line last-result summary when results exist.

Keep the safety distinction visible:

- QC: read-only.
- Inspection / Instance Builder: add unconstrained components to the active assembly and never save automatically.

Add Instance Builder to Overview navigation and remove stale four-tool wording from README/architecture documentation.

## Shared layout specification

Use the same vertical rhythm for all functional tabs:

1. Tool title + one concise description.
2. Context/target if the tool depends on the current Creo model.
3. Source.
4. Options.
5. Primary action + compact progress.
6. Results heading + summary/filter/search controls.
7. Expanding table.
8. Selected-row details.
9. Contextual result actions.
10. Shared global footer.

### Spacing and density

Use a small set of repeatable offsets instead of per-tab tuning:

- outer tab padding: 8-12 native units,
- tight inline spacing within one control row,
- larger separation between conceptual sections,
- no long sequences of more than roughly four independent controls in one horizontal row at the minimum supported width.

Do not rely on a wide monitor to make the UI readable. Design first for 1366x768 at 100% scaling, then verify 1920x1080 at 100% and 150%.

### Labels and microcopy

- Prefer short section labels: `Source`, `Options`, `Placement`, `Results`.
- Keep safety guidance close to the action it affects instead of repeating long prose at the top and bottom.
- Avoid sentence-length checkbox labels when the same meaning can be expressed as a group label plus short options.
- Use consistent verbs:
  - `Check dimensions`
  - `Check accuracy`
  - `Add to assembly`
  - `Plan positions`
  - `Find and add instances`
- Use consistent empty states: `No results`, `No matching results`, `No plan`, `No active assembly`.

## Global footer and operation feedback

The footer remains visible on every tab and is the authoritative operation status.

Recommended footer content:

- left: `Tool | State | current item` or completion summary,
- center/right: progress/cancel when active,
- far right: Close.

Rules:

- Per-tab progress text should be short, for example `32 / 120 models` or `8 / 25 codes resolved`.
- Do not repeat the entire global status message next to every progress bar.
- Cancel should only be enabled while an operation can accept cancellation.
- Switching tabs must not hide which tool owns the active operation.
- Closing during a run follows the existing cancellation contract; v0.4.0 should improve wording, not change safety semantics.

## Results design system

Use a common result-review pattern across all tools.

### Table rules

- Keep status visible without horizontal scrolling at the supported minimum size.
- Prefer 4-6 useful columns over wide catch-all tables.
- Remove the free-text `Details` column from a table when the same information is available in the selected-row details area.
- Keep stable internal row identity separate from visible sort/filter order.
- Preserve selection after refresh when the selected result still exists.
- If the Toolkit supports reliable sortable headers without large custom logic, allow sorting by status/name as a stretch goal; do not make it a release blocker.

### Details area

The details area should show full context for the selected row:

- full source path,
- model/source identity,
- feature/section/dimension/component identifiers where applicable,
- complete diagnostic or result text,
- target/placement information when relevant.

Prefer a multiline read-only native control if it renders and scales well in Creo. If that control is not acceptable, use concise wrapped labels and retain complete detail in CSV/log output.

### Filters

Weak Dimensions and Accuracy keep search + Issues-only behavior, but the toolbar should be visually aligned and consistent.

Add status review filters to Inspection and Instance Builder if native control density remains acceptable:

- Inspection: All / Problems, where Problems = Warning, Failed, or Skipped.
- Instance Builder: All / Unresolved, where Unresolved = Not Found, Failed, or Skipped.

If a segmented/radio presentation is not reliably supported, use one compact checkbox (`Problems only` / `Unresolved only`).

## Weak Dimensions

Goal: make the most important flow read as `choose source -> check -> review findings`.

Changes:

- Keep Creo selection as the default source.
- Visually group folder-only options below the folder path rather than on the same line as unrelated status text.
- Move discovery/source feedback to its own concise row.
- Keep `Check dimensions` as the only primary action.
- Align `Issues only` and Search with Accuracy.
- Reduce table columns to the information needed for scan decisions. Proposed visible columns:
  - Part
  - Status
  - Feature
  - Section
  - Weak dim
- Move finding detail text and full source path to selected-row details.
- Keep `Open part`, `Go to feature`, and `Clear results` together under the details area; enable actions only when valid.

## Accuracy

Goal: make rule compliance obvious without spending excessive vertical space on setup.

Changes:

- Keep the required rule persistently visible but compact: `Required: ABSOLUTE = 0.001`.
- Use the same source/folder pattern as Weak Dimensions.
- Put Parts / Assemblies under a clear `Model types` sublabel when folder mode is active.
- Keep `Check accuracy` as the only primary action.
- Proposed visible table columns:
  - Model
  - Kind
  - Accuracy type
  - Accuracy
  - Status
- Move long details/path text below the table.
- Keep `Open model` and `Clear results` in the same location/pattern as Weak Dimensions.

## Inspection

Goal: reduce setup density and make placement behavior understandable before components are added.

### Target

Show active assembly immediately below the title. Keep the warning concise:

`Active: <assembly> | Adds unconstrained components; no automatic save.`

If no assembly is active, keep the primary Add action disabled and explain the required correction in the same location.

### Source

Split discovery settings into two logical rows instead of one dense row.

Suggested structure:

- Folder + Browse
- Discovery: Include subfolders | Latest version only
- Types: Parts | Assemblies | STEP
- Family table: Instances | Also generic
- Discovery summary

### Placement

Replace the current long single placement row with explicit concepts:

- Arrangement: Auto arrange / Same origin
- Row direction: default direction / rows along X
- Plane: X-Y / X-Z
- Grid: Columns | Gap

The exact native controls depend on Toolkit support. Radio controls are preferable for mutually exclusive choices; otherwise preserve checkboxes but use short labels and clear group headings.

P0 correctness check: `useZAxisForRows` must be read from the UI, retained in session state, applied to `InspectionOptions`, and reflected when the dialog reopens. If the option is not supported end-to-end, remove the control rather than shipping a non-functional setting.

### Results

Proposed visible columns:

- Source
- Kind
- Added model
- Status
- Feature ID

Move verbose details to the selected-row panel.

Add `Problems only` if it does not compromise minimum-width fit.

Consider a contextual `Open added model` or `Locate component` action only if the existing Creo APIs can implement it reliably without expanding release risk; this is P2, not required.

## Instance Builder

Goal: turn the newest and densest tool into a guided workflow rather than a long form.

Use three explicit stages.

### 1. Input

- Active target assembly status.
- Search folder + Browse.
- Include subfolders / Latest version only.
- Requested codes text area.
- Short parser guidance below the label rather than in a long title.

As the user edits codes/columns, show a concise validation summary when feasible:

`24 requested codes | 5 columns | 5 planned rows`

Do not start source-model scanning while typing.

### 2. Plan

Allocation controls:

- Columns
- Gap
- `Plan positions` secondary action

Planning should be a visible state transition, not just a table mutation. After planning:

- keep requested count visible,
- show rows/columns allocation in the table,
- make it clear that no source models have been searched yet.

The plan can be invalidated when codes or allocation settings change. If detecting every edit reliably is expensive in the native UI, invalidate on the next Plan/Build action and show clear stale-plan wording.

### 3. Build and review

`Find and add instances` is the primary action.

Before enabling it, require:

- active assembly,
- non-empty valid requests,
- valid columns/gap,
- non-empty source folder.

During the run, global status identifies current source-model search and resolved count. Cancellation retains the current safety behavior: cancel during search adds no components because assembly occurs after search completion.

After completion, prioritize unresolved requested codes in the review experience.

Proposed visible table columns:

- Requested code
- Row
- Column
- Generic
- Added model
- Status

Move Feature ID, full source path, and long details to selected-row details.

Add `Unresolved only` if the layout fits cleanly.

Keep `Export CSV report` as a secondary result action and rename `Clear` to `Clear results` for consistency.

## P0 consistency and correctness audit

Before visual polish, complete a control-to-state audit for every resource component.

For each interactive component verify:

- callback registered,
- current value read,
- state persisted when expected,
- enable/disable behavior correct during operations,
- value influences the intended tool option,
- restored state matches the active behavior.

Known item to verify first: Inspection X-Z / `InspUseZAxis` behavior.

Also verify:

- Instance Builder appears on Overview.
- Overview status does not imply only four tools exist.
- README architecture lists Instance Builder.
- v0.4.0 changelog will include the Instance Builder integration baseline if it remains absent from prior release notes.
- duplicated resource files are byte-equivalent.

## Engineering enablers

v0.4.0 is a UX release, but small internal refactors are justified when they reduce inconsistency.

Recommended:

- add shared helpers for common result-summary and status-filter behavior,
- add a small shared helper for active-assembly status formatting used by Inspection and Instance Builder,
- centralize standard button labels and common empty-state wording where practical,
- keep Instance Builder UI logic modular rather than moving it back into `ToolboxDialog.cpp`,
- consider extracting QC table/detail refresh helpers only if it clearly reduces duplicated state bugs,
- add a packaging/release check that confirms both `.res` copies match.

Do not block the release on a large `ToolboxDialog.cpp` rewrite.

## Implementation sequence

| Phase | Deliverables | Completion criteria |
| --- | --- | --- |
| 1. Native baseline | Capture screenshots of all five tabs and key states; record actual dialog size at 1366x768/100%, 1920x1080/100%, 1920x1080/150%; audit every control against callbacks/state. | Baseline is documented; orphaned controls and clipping/density issues are listed; P0 control-state defects are identified. |
| 2. Shell and navigation | Update Overview for five tools; align tab copy; simplify shared footer/status; establish spacing/label conventions. | Every tool is discoverable from Overview; safety distinction is visible; footer owns global operation status. |
| 3. QC refinement | Reflow source/options rows; standardize search/filter toolbar; reduce table width; strengthen details/action area. | Weak and Accuracy share one recognizable interaction pattern; no duplicated long details columns are required for normal review. |
| 4. Assembly-tool refinement | Reflow Inspection source/placement groups; wire/fix X-Z placement; redesign Instance Builder into input/plan/build states; add result filters where useful. | Assembly target and impact are obvious; Instance Builder can be completed without interpreting a dense long form; all controls affect real behavior. |
| 5. Result-review polish | Standardize summaries, empty states, details, contextual action placement, selection preservation, and filter behavior. | Result review remains usable for large runs; filtering never breaks row-to-result identity; details remain accessible without extreme table width. |
| 6. Validation and release | In-Creo UAT, keyboard/scaling checks, docs/changelog/version sync, build/unlock, package, installed-package smoke test. | Acceptance checks pass and visual/runtime evidence is captured before version bump/tag. |

## Acceptance checks

1. **Five-tool navigation:** every tool is reachable from Overview and by tab; Overview copy correctly distinguishes QC from assembly-modifying tools.
2. **Minimum-size fit:** at 1366x768/100%, primary actions, result status, and essential setup remain reachable without requiring an unnecessarily wide window.
3. **Scaling:** verify 1920x1080 at both 100% and 150%. Labels do not clip, table headers remain readable, and controls do not collide.
4. **Visual hierarchy:** ask two representative users to identify the primary action and current target/source on each tab without coaching. Misidentification is a release blocker.
5. **Keyboard:** tab order follows visual order; disabled controls are skipped appropriately; focus is visible; Enter/Escape do not trigger destructive or surprising behavior.
6. **Control correctness:** every visible option changes or reflects actual behavior. No orphaned checkboxes, stale restored values, or decorative controls remain.
7. **Result review:** selected identity survives refresh/filter where possible; opening/highlighting actions resolve the correct underlying result after filtering.
8. **Large results:** inspect at least 1,000 QC findings and a large Instance Builder request list. Table refresh and selection should remain usable; reproducible UI stalls caused by unnecessary full-table rebuilds should be addressed or documented.
9. **Assembly safety:** Inspection and Instance Builder always show the active target before execution, add unconstrained components only, and never save automatically.
10. **Cancellation:** active-operation ownership remains visible while switching tabs; Cancel wording and enablement match the real cancellation boundary.
11. **Resource parity:** both resource files are identical in the packaged release.
12. **Documentation:** README, changelog, release plan, VERSION metadata, and architecture wording describe the same set of tools and v0.4.0 behavior.

## Success measures

Use lightweight usability metrics rather than subjective visual preference alone.

For three representative tasks, compare baseline v0.3.0/current-main behavior to the v0.4.0 candidate:

- run folder Accuracy QC and inspect one failing model,
- build an Inspection assembly with non-default placement,
- paste a list of Instance Builder codes, review allocation, build, and identify unresolved codes.

Record:

- time to first correct primary action,
- number of wrong clicks/mode errors,
- number of labels requiring explanation,
- whether the user notices the active assembly and no-auto-save behavior,
- whether the user can find result details without scanning a wide table.

Target: no critical misunderstandings, fewer mode/action errors than baseline, and no task requiring coaching due to layout or terminology.

## Scope control

Required for v0.4.0:

- five-tool Overview/navigation consistency,
- P0 control/state audit and fix,
- shared spacing/hierarchy rules,
- lower-density Inspection layout,
- guided Instance Builder input/plan/build flow,
- narrower scannable result tables with stronger detail panels,
- consistent global operation feedback,
- keyboard/scaling/native Creo UAT,
- docs/version/resource consistency checks.

Good stretch items:

- Problems/Unresolved filters for assembly tools,
- limited result sorting if the native API is reliable,
- contextual locate/open actions for assembly result rows,
- reusable helper code that reduces duplicated UI state logic.

Defer beyond v0.4.0:

- full custom visual theming,
- icon pack or custom graphics system,
- UI framework migration,
- cross-session preference storage,
- persisted run history/dashboard,
- automatic model repair,
- inspection rollback/undo engine,
- drag-and-drop source input,
- arbitrary resizable split panes unless Creo native support is proven stable,
- redesign of underlying QC algorithms unrelated to UX correctness.

## Release checklist

- [ ] Capture baseline screenshots before implementation.
- [ ] Resolve the Inspection X-Z control/state gap.
- [ ] Add Instance Builder to Overview.
- [ ] Update five-tool architecture/docs wording.
- [ ] Rework resource layout in both resource copies.
- [ ] Verify both resource copies are identical.
- [ ] Complete 1366x768 and 150% scaling checks.
- [ ] Complete keyboard/focus walkthrough.
- [ ] Run result-scale tests.
- [ ] Complete Inspection and Instance Builder safety/cancellation regression.
- [ ] Update `docs/CHANGELOG.md` for 0.4.0.
- [ ] Synchronize `VERSION.txt`, `include/app/Version.h`, CMake version metadata, dialog title, and packaging paths only when release implementation is accepted.
- [ ] Build and unlock with the supported Creo/Pro TOOLKIT environment.
- [ ] Package and smoke-test the installed release.

The central v0.4.0 design decision is to spend screen space on the user's current task and result interpretation, not on repeated explanatory text or duplicate detail columns. The release should feel calmer and more predictable without changing the native Creo character of the product.