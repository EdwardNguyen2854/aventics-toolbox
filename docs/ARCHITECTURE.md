# Aventics Toolbox v0.4.0 Architecture

## Product boundary

One external Creo Pro/TOOLKIT application:

```text
Creo 9
  -> protk.dat
  -> aventics_toolbox.dll
  -> native tabbed Aventics Toolbox dialog
```

Tools are internal C++ modules, not separate DLLs.

## UI

Top-level native Creo tabs:

- Overview
- Weak Dimensions
- Accuracy
- Inspection
- Instance Builder

The tabs keep their in-session state when the user switches between them. Overview distinguishes read-only QC from the two assembly-modifying tools and links all four functional tabs directly.

The common functional-tab hierarchy is:

```text
context/target -> source -> options -> primary action -> results -> selected details/actions
```

Weak Dimensions and Accuracy each expose one primary run action and switch between Creo selection and folder input. Their result tables support text search, an Issues-only view, selected-row details, and contextual model actions. v0.4.0 removes long free-text diagnostic columns from the visible table so identity/status remain scannable at smaller widths.

Inspection groups its active target, source discovery, source types, family-table choices, and placement settings. Auto arrange supports row progression, X-Y or X-Z placement planes, columns, and gap; disabling Auto arrange uses same-origin placement. The X-Z choice is retained for the current Creo session and read by `InspectionBuilder` when a run starts.

Instance Builder is intentionally modular in `InstanceBuilderUi.cpp` and presents three stages:

```text
Input -> Plan -> Build and review
```

Its review table keeps code, allocated cell, resolved generic/model, and status visible while full source/feature/diagnostic data remains in selected-row details or CSV. An Unresolved-only view filters Not Found, Failed, and Skipped rows without changing underlying result identity.

A shared footer reports the owning operation and cancellation state from every tab. While an operation runs, callbacks guard source/run/clear changes so the frozen queue cannot be replaced. Tab navigation and result review remain available.

## Shared services

```text
FolderScanner
  -> parses .prt/.asm Creo versions and .stp/.step
  -> latest-version resolver

SelectionService
  -> selected Creo parts/assemblies
  -> duplicate model filtering

ModelLoader / SessionSnapshot
  -> retrieve full paths without changing working directory
  -> preserve models already in the user's Creo session
  -> erase temporary folder-QC models after each check

OperationRunner
  -> Creo UI timer
  -> one model per UI tick
  -> shared progress state and safe cancellation
```

No Creo model API is intentionally called from a background worker thread.

## QC tools

### Weak Dimensions

Parts only. Existing proven algorithm is retained:

```text
feature
 -> section copy
 -> dimension IDs
 -> fresh section copy for each dimension
 -> ProSecdimStrengthen(temp copy, id)
```

`PRO_TK_NO_ERROR` means the temporary weak dimension was successfully strengthened, so the original dimension is reported as weak. The database-owned section is never changed.

### Accuracy

Parts and assemblies. Read-only `ProSolidAccuracyGet()` check:

```text
PASS = ABSOLUTE and value = 0.001 (within floating comparison epsilon)
```

## Inspection Assembly Builder

This tool intentionally modifies only the currently active assembly by adding component features. It never saves automatically.

Sources:

- Creo `.prt` / `.prt.N`
- Creo `.asm` / `.asm.N`
- family-table instances
- STEP `.stp` / `.step`

Native models are loaded with full-path retrieval. STEP files are imported to temporary names such as `AVT_STEP_0001` and the top-level imported model is assembled once.

Components are assembled by `ProAsmcompAssemble()` without adding constraints.

Placement modes:

- Auto arrange: bounding-box based grid
- Same origin: identity transform
- Row direction: default or completed rows advance along X
- Plane: X-Y or X-Z

## Instance Builder

Instance Builder accepts requested codes, allocates deterministic row/column cells, searches exact standalone/family-instance names, and assembles resolved models unconstrained to the active assembly.

Search and assembly are intentionally separated:

```text
parse/allocate
 -> discover source models
 -> resolve requested codes through OperationRunner
 -> if search completes, assemble resolved requests into planned cells
```

This makes cancellation during search safe: no new Instance Builder components are added until source resolution finishes. Duplicate requested codes keep independent planned cells and missing codes leave holes instead of shifting later components.

Final results can be exported as UTF-8 CSV.

## UI state

`AppContext` retains session-level UI state including source paths, source modes, filters, Inspection placement options, Instance Builder codes/allocation options, the Inspection X-Z plane choice, and the Instance Builder Unresolved-only review choice.

The two `.res` files are intentionally mirrored. `package-release.ps1` verifies SHA-256 parity before creating the release package.

## Safety rules

- Weak Dimension and Accuracy checks are read-only.
- No automatic `ProMdlSave`.
- No automatic weak-dimension repair.
- No accuracy correction.
- Folder QC never erases models that existed in the session before that model's scan.
- Inspection and Instance Builder do not erase models used by components they add.
- Inspection Cancel stops between models; it does not roll back already-added components.
- Instance Builder Cancel during source search adds no components because assembly is deferred until resolution completes.
