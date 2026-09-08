# Aventics Toolbox v0.5.2 Architecture

## Product boundary

One external Creo Pro/TOOLKIT application:

```text
Creo 9
  -> protk.dat
  -> aventics_toolbox.dll
  -> Aventics Toolbox Control Panel
       -> Weak Dimensions GUI
       -> Accuracy GUI
       -> Inspection Builder GUI
       -> Instance Builder GUI
```

The tools remain internal C++ modules in one DLL. v0.5.2 keeps the v0.5.x standalone-dialog architecture and refines the native UI hierarchy without changing the application/DLL boundary.

## UI shell

The Control Panel is the entry GUI. Selecting a tool exits the launcher view and opens that tool's standalone native Creo dialog.

The v0.5.2 shell follows a consistent hierarchy:

```text
header
  -> Toolbox navigation
  -> tool title / context
  -> Maximize / restore
  -> Close

setup / primary action
  -> source and options
  -> one main operation

results
  -> summary + filters
  -> expandable table
  -> selected-row details
  -> secondary actions

footer
  -> operation status
  -> Cancel
```

The Control Panel groups read-only QC separately from assembly-modifying tools and shows the current active Creo model. Assembly-tool context explicitly indicates whether an active assembly is available. Inspection Builder and Instance Builder continue to never save automatically.

Weak Dimensions and Accuracy retain their existing selection/folder boolean state for compatibility, but the user-facing wording now presents it clearly as **Use selected parts/models** with folder controls acting as the alternate source.

Inspection Builder uses a two-column setup area: **Source** contains discovery/model/family filters and **Placement** contains arrangement, direction, plane, columns, and gap.

Instance Builder presents the existing logic as four explicit stages: **1 Source -> 2 Requested Codes -> 3 Layout -> 4 Build**. The deterministic plan/result summary is placed in the Results header so requested/resolved/missing counts stay visible with the filter controls.

Tool navigation is blocked while an operation is active so an in-progress dialog is not destroyed. Session state is stored in `AppContext`, therefore paths, filters, results, placement settings, and Instance Builder input survive navigation between the Control Panel and tool dialogs.

The native resources remain split into five files:

```text
aventics_toolbox.res            # Control Panel
aventics_weak.res               # Weak Dimensions
aventics_accuracy.res           # Accuracy
aventics_inspection.res         # Inspection Builder
aventics_instance_builder.res   # Instance Builder
```

All five files are mirrored under `text/usascii/resource`. `package-release.ps1` verifies SHA-256 parity for every resource pair before packaging. Labels remain ASCII-only.

## Window sizing

The v0.5.2 **Maximize / restore** action replaces the one-way v0.5.1 full-screen behavior. Before maximizing, the controller reads the dialog's current horizontal and vertical screen-relative sizes with `ProUIDialogHorzsizeGet()` and `ProUIDialogVertsizeGet()`. It then expands the active tool dialog to the screen-relative maximum using the existing size-set APIs. A second activation restores the saved dimensions.

The saved maximize state is scoped to the currently active tool dialog and resets when navigating to another standalone view.

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

## Inspection Builder

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

The request field uses a native multiline area with a 32,767-character maximum length. The shared textarea helper checks the current enabled state before changing sensitivity, preventing input callbacks from unnecessarily re-enabling the focused multiline control during a paste.

Search and assembly remain intentionally separated:

```text
parse/allocate
 -> discover source models
 -> resolve requested codes through OperationRunner
 -> if search completes, assemble resolved requests into planned cells
```

The allocation is fixed before source search. If a requested instance cannot be resolved, that request is reported as unresolved and its preallocated grid position remains empty. The assembler then continues with the next request at its own preallocated row/column, so later components do not shift into the missing position.

Duplicate requested codes keep independent planned cells. Final results can be exported as UTF-8 CSV.

## UI state

`AppContext` retains session-level UI state including source paths, source modes, filters, Inspection placement options, Instance Builder codes/allocation options, the Inspection X-Z plane choice, and the Instance Builder Unresolved-only review choice.

## Safety rules

- Weak Dimension and Accuracy checks are read-only.
- No automatic `ProMdlSave`.
- No automatic weak-dimension repair.
- No accuracy correction.
- Folder QC never erases models that existed in the session before that model's scan.
- Inspection Builder and Instance Builder do not erase models used by components they add.
- Inspection Cancel stops between models; it does not roll back already-added components.
- Instance Builder Cancel during source search adds no components because assembly is deferred until resolution completes.
- Tool navigation is prevented during an active operation.
