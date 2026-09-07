# Aventics Toolbox v0.3.0 release plan

Status: implemented; native in-Creo visual acceptance pending. Prepared 2026-09-07 against the local v0.2.0 source.

## Release objective

Make the existing engineering tools easier to discover, configure, run, and interpret through a consistent native Creo interface. Success means users can identify the next action, understand operation status, and navigate from a finding to the relevant model without guessing.

Keep the existing Overview, Weak Dimensions, Accuracy, and Inspection tabs. Use Creo-native controls and typography, restrained emphasis, consistent spacing, readable labels, and a results-first layout. Proposed version: 0.3.0; update version metadata only when preparing the release.

## Evidence and boundaries

This is a source-based assessment, not a visual review of the running dialog. Capture the actual v0.2.0 interface in Creo before finalizing dimensions and styling.

| Current evidence | Release response |
| --- | --- |
| QC tabs show folder inputs and two competing scan buttons together. | Explicit source choice with one primary run action per tool. |
| Resource layouts use 72-column folder inputs, wide tables, and different visible-row counts. | Verify minimum window size, align controls, allocate expansion to results, and provide full details separately. |
| Inspection places progress below results; QC places it above. | Consistent operation feedback and a global status area visible from every tab. |
| `OnClose` writes cancellation feedback to Inspection regardless of which tool runs. | Route feedback through shared operation state. |
| Folder scan handlers can replace pending work before the later running check; Clear handlers have no running guard. | Guard every mutating UI callback and freeze the active operation's inputs and queue. |
| `InspClearButton` is declared and registered but absent from the Inspection layout. | Place it consistently with other result actions. |
| Inspection number parsing clamps or silently defaults invalid input. | Inline validation with clear correction guidance. |
| Found counts refresh on Browse and Run, but no edit/option-change callbacks are registered. | Invalidate stale counts and refresh discovery deliberately. |
| Result rows are reconstructed after each processed source. | Preserve selected result identity and scroll position; assess incremental updates for larger result sets. |
| AppContext retains folders and results; inspection columns and gap reset on dialog creation. | Retain source options, placement values, filters, and active tab for the Creo session. |

Primary implementation areas: `text/resource/aventics_toolbox.res`, its `text/usascii/resource` copy, `src/app/ToolboxDialog.cpp`, `src/common/UiUtils.cpp`, `src/common/OperationRunner.cpp`, and `include/app/AppContext.h`.

## Proposed interface

Each tool follows the same vertical arrangement:

1. Tool title and one short explanation; show the accuracy rule or active target where relevant.
2. Source and options, with clearly associated field labels.
3. Primary action and concise preflight feedback.
4. Result counts and filters.
5. Expanding result table, followed by selected-row details and contextual actions.
6. Shared footer containing active operation, current source, processed/total, cancellation, and Close.

Visual rules: use sentence case, align field starts and button groups, apply one spacing rhythm, and keep model identity and status visible at the supported minimum size. Use text labels for every status; color is optional reinforcement where native controls support it. Put lengthy technical details in the details area. Do not require a custom font, web renderer, or new UI framework.

### Overview

- Shorten descriptions to the purpose and supported input of each tool.
- Keep direct tool navigation and Run All QC on Creo Selection.
- Show the combined run state and a completion summary with links/buttons to each QC tab.
- Make the distinction between read-only QC and assembly-building behavior visible near the relevant action.

### Weak Dimensions and Accuracy

- Introduce a source selector: Creo selection or Folder. Proposed initial default: Creo selection; retain the user's choice during the session.
- Folder mode shows a labeled path, Browse, applicable filters, and discovery status. Selection mode explains that Run opens Creo selection and that cancelling selection leaves prior results intact.
- Use one primary action: Check dimensions or Check accuracy. Accuracy shows the required ABSOLUTE = 0.001 rule persistently.
- Include All / Issues / Passed result views and model-name search. Define Issues as Fail, Warning, or Error; keep skipped results visible through All and an explicit status filter.
- Display model counts separately from finding counts. Filtering must not change the underlying run totals.
- Enable Open model and Go to feature only for actionable selections. Report loading/highlighting failures visibly.
- Preserve full source path and feature/section/dimension identifiers in selected-row details.

### Inspection

- Group setup into Target assembly, Source files, and Placement.
- Show the target assembly name and explain that components are added without constraints and are not automatically saved.
- Group native files, STEP, and family-table options; explain the relationship between instances and generics.
- Offer Auto arrange / Same origin. Enable columns and gap only when Auto arrange is selected.
- Require columns to be an integer >= 1 and gap to be a finite number >= 0. Reject trailing garbage and invalid values. Keep the explicit assembly-units label; resolve the actual unit name only if the SDK supports it reliably.
- Discovery counts represent source files, not promised component counts: family expansion and STEP processing can change the number of added components.
- Use a primary action such as Add to assembly, with the target and impact clearly visible before activation. Revalidate the active assembly at execution time.
- Show Added / Warning / Failed / Skipped totals and full selected-source details. Include Clear results in the layout and state that it only clears the displayed report.
- On cancellation, state that already-added components remain and the assembly has not been saved automatically.

## Implementation sequence

| Phase | Deliverables | Completion criteria |
| --- | --- | --- |
| 1. Baseline and design specification | Capture all tabs and important states in Creo; record common screen sizes; draft annotated layouts for QC and Inspection; check local SDK support for layout, visibility, focus, filtering, and table updates. | Layout and state specification covers every existing option and action, including Run All QC. Unsupported controls have a native fallback. |
| 2. Shared layout and visual consistency | Rework both resource copies; consistent headings, labels, spacing, action locations, details area, and resize behavior; simplify Overview. | No missing controls or clipped primary actions in the agreed display matrix; both resource copies match. |
| 3. Source setup and operation UX | Source modes, input validation, fresh discovery status, session state, shared operation feedback, button enablement, cancellation and close handling. | Invalid or conflicting actions cannot start or alter a run; state and partial-result behavior remain clear across tabs. |
| 4. Results workflow | Search/status filters, accurate summary counts, stable row mapping, selected-row details, contextual actions, preserved selection during refresh. | Filtering/sorting never opens the wrong model or feature; full details are accessible; repeated updates remain usable. |
| 5. Validation and release | In-Creo UAT, regression checks, screenshots, updated user guide/changelog, version synchronization, build/unlock, package and installed-package smoke test. | All required acceptance checks below pass; unresolved issues are documented and assessed before shipping. |

Dependencies: phase 1 precedes implementation; phase 2 establishes the controls used by phase 3; phase 4 integrates with phase 3 state management; phase 5 validates the completed release. Work on copy and test fixtures can proceed alongside implementation.

## Operation behavior contract

- States: Idle, Discovering, Selecting, Running, Cancelling, Completed, Cancelled, and Failed. Distinguish a completed run with item errors from an operation that could not start or continue.
- One operation owns a frozen queue and options. Disable conflicting run, source-edit, and clear actions across all tabs, with guards in callbacks as well as visual disablement.
- Keep tab navigation available for reviewing results. Global feedback identifies the owning tool, including Run All QC.
- Cancel changes feedback immediately to Cancelling; processing stops at the next supported boundary between source models. Do not promise interruption inside a Creo model API call.
- Close during a run requests cancellation and displays the same visible guidance from every tab. Destroy the dialog only after the timer/operation has stopped; a subsequent Close can exit.
- Invalid setup, empty discovery, cancelled selection, and startup failures keep previous results and identify them as belonging to the previous run. Replace them only once a new operation starts successfully.
- Preserve partial results on cancellation or failure, with processed and unprocessed counts. Surface timer/start failures and unexpected processing errors instead of presenting normal completion.
- Folder enumeration is currently synchronous. Measure it with a representative large directory; if it blocks required UI responsiveness, split filesystem discovery into bounded steps before enabling automatic refresh. Keep Creo model calls on the UI thread.

## Release acceptance checks

1. **Visual fit:** inspect every tab at 1366x768/100%, 1920x1080/100%, and 1920x1080/150% Windows scaling as the proposed baseline. Adapt the minimum supported size from native testing. Primary actions, source labels, and status remain reachable; resize gives additional room to results.
2. **Keyboard:** complete source setup, run, result selection, and close using keyboard navigation; verify visible focus, logical tab order, and no accidental execution from Enter/Escape.
3. **Input states:** cover empty/missing folder, no eligible files, no selected file types, cancelled selection, invalid columns/gap, missing active assembly, and unavailable source models.
4. **Concurrent actions:** attempt Browse, Run, Clear, source changes, tab switches, and Close during each tool and Run All QC. The active queue stays intact and cancellation feedback remains visible.
5. **Results:** verify empty, pass-only, mixed, error, skipped, and cancelled outcomes; filter and search a fixture with repeated model names in different folders and multiple findings per part. Actions resolve the exact underlying source/finding.
6. **Scale:** review a representative run of at least 100 sources and a result set of at least 1,000 findings. Record refresh behavior and keyboard/scroll responsiveness; eliminate reproducible UI stalls attributable to rebuilding the result table. Report long individual model calls separately.
7. **State retention:** switch tabs and reopen the toolbox in the same Creo session; paths, source modes, applicable options, placement values, and result filters are restored consistently.
8. **Engineering regression:** verify QC remains read-only, latest-version filtering remains correct, pre-existing session models remain loaded, and Inspection adds unconstrained components without automatic saving or cancellation rollback.
9. **Usability walkthrough:** ask two representative users to check a folder, inspect a weak-dimension finding, and build an inspection assembly without coaching. Record baseline versus candidate task time, missteps, and misunderstood labels. Resolve blockers before release.
10. **Packaging:** build and unlock for the existing Creo 9.0.2.0 target; test installed resources from the release package. Synchronize CMake, Version.h, VERSION.txt, both dialog resources, and relevant docs/scripts for 0.3.0.

Use focused tests for validation, filtering, result identity, and operation-state transitions where these can be separated from the SDK. Native rendering and Creo interaction require in-Creo checks; compilation alone is insufficient.

## Scope control

Required: consistent layout, explicit source selection, validation, shared operation state, result filtering/details, keyboard and scaling checks, session-level preference retention, and release verification.

Defer: CSV/Excel export, persisted run history, cross-session preferences, configurable QC rules, automatic repairs, inspection rollback, custom STEP profiles, theme engines, and a UI-framework migration. These can become later releases without diluting the UX work.

Remaining design decisions to resolve in phase 1: actual minimum dialog size, native details-panel arrangement, initial result filter (proposed All), and whether filesystem discovery needs chunking. The source audit does not establish runtime appearance, performance, or SDK feasibility for new widgets.
