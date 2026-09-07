# Aventics Toolbox v0.3.0 Architecture

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

The tabs keep their in-session state when the user switches between them.

The v0.3.0 interaction model is consistent across tools:

```text
choose source -> configure options -> run -> filter/review results -> open model or feature
```

Weak Dimensions and Accuracy each expose one primary run action and switch between Creo selection and folder input. Their result tables support text search, an Issues-only view, selected-row details and contextual model actions. Inspection groups its active target, source filters and placement settings, with strict validation for columns and gap. Auto arrange supports both the original Y-axis row progression and an X-axis horizontal row progression.

A shared footer reports the owning operation and cancellation state from every tab. While an operation runs, callbacks guard and disable source, run and clear controls so the frozen queue cannot be replaced. Tab navigation and result review remain available.

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

## Safety rules

- Weak Dimension and Accuracy checks are read-only.
- No automatic `ProMdlSave`.
- No automatic weak-dimension repair.
- No accuracy correction.
- Folder QC never erases models that existed in the session before that model's scan.
- Inspection Builder does not erase models used by components it has added.
- Cancel stops between models; it does not roll back already-added inspection components.
