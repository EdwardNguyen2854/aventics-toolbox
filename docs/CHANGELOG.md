# Changelog

## 0.4.0

- Reworked the native Creo layout around clearer Source, Placement, Results, and staged workflow sections.
- Added Instance Builder to Overview so all five tools are directly discoverable from the landing tab.
- Reduced wide result tables by moving long diagnostic text, paths, and feature details into the selected-result area.
- Split Inspection discovery, model-type, family-table, placement-direction, plane, and grid settings into lower-density rows.
- Retained the Inspection X-Z placement-plane choice across dialog reopen and kept it connected to the existing placement engine behavior.
- Redesigned Instance Builder as an explicit Input -> Plan -> Build and review workflow.
- Added Instance Builder input-aware action enablement and clearer stale-plan guidance when inputs change.
- Added an Unresolved-only Instance Builder result filter while preserving stable row-to-result mapping.
- Shortened Instance Builder local progress text while keeping the global footer authoritative for operation ownership.
- Renamed Instance Builder Clear to Clear results and kept CSV export as a secondary result action.
- Added packaging validation that refuses to ship when the two mirrored native resource files differ.
- Synchronized v0.4.0 version metadata, resource title, release package names, documentation, and five-tool architecture wording.
- Includes the previously merged Instance Builder baseline: code-list parsing, deterministic row/column allocation, exact standalone/family-instance resolution, unconstrained assembly placement, missing-code cell preservation, cancellation-before-assembly safety, and CSV result export.

## 0.3.0

- Redesigned all native tabs with consistent headings, spacing, action placement and resize behavior.
- Added an explicit Creo-selection or folder source mode and one primary run action to each QC tool.
- Added model-name/details search and Issues-only filtering to QC results.
- Added selected-result details and contextual action enablement.
- Added shared operation status and cancellation feedback visible from every tab.
- Prevented browse, run and clear actions from changing an active operation.
- Added strict Inspection columns/gap validation and disabled placement inputs when Auto arrange is off.
- Added an Inspection option to arrange completed rows along the X axis (horizontal) instead of Y.
- Grouped Inspection target, source and placement settings and restored the missing Clear results action.
- Preserved paths, source modes, filters and placement options while reopening the dialog in a Creo session.
- Clarified partial-result, selection-cancel and inspection no-save behavior.

## 0.2.0

- Renamed product to Aventics Toolbox.
- Replaced launcher-oriented parent UI with native tabbed Creo workspace.
- Added Overview, Weak Dimensions, Accuracy and Inspection tabs.
- Preserved Run All QC for selected Creo models.
- Added folder-based Weak Dimension scanning for all parts.
- Added folder-based Accuracy scanning for parts and assemblies.
- Added Creo version parsing and latest-version deduplication.
- Added optional recursive folder traversal.
- Added session-safe batch model loading/cleanup.
- Added timer-driven progress and cancellation.
- Added Inspection Assembly Builder.
- Added native part/assembly placement without constraints.
- Added family-table instance placement.
- Added STEP `.stp/.step` import and placement.
- Added auto-arrange bounding-box grid and same-origin mode.
- Added user-level release installer scripts.
