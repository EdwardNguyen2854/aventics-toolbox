# Creo 9 Pro/TOOLKIT API Notes

Important APIs used by v0.3.0:

## UI

- `ProUIDialogCreate`, `ProUIDialogActivate`, `ProUIDialogDestroy`
- `ProUITabLabelsSet`, `ProUITabSelectednamesSet`, `ProUITabDecorate`, `ProUITabShow`
- `ProUITable...`
- `ProUIInputpanel...`
- `ProUICheckbuttonGetState`
- `ProUIProgressbar...`
- `ProUITimerCreate`, `ProUIDialogTimerStart`, `ProUITimerDestroy`

The timer runner is used only to defer work back through Creo's UI execution path. It is not a worker thread.

## Model source/session

- `ProSelect`
- `ProSelectionAsmcomppathGet`
- `ProAsmcomppathMdlGet`
- `ProMdlFiletypeLoad`
- `ProSessionMdlList`
- `ProMdlErase`

## Weak dimensions

- `ProSolidFeatVisit`
- `ProFeatureNumSectionsGet`
- `ProFeatureSectionCopy`
- `ProSecdimIdsGet`
- `ProSecdimStrengthen`
- `ProSectionFree`

A fresh temporary section copy is used for every dimension test.

## Accuracy

- `ProSolidAccuracyGet`

## Family tables

- `ProFamtableInit`
- `ProFamtableInstanceVisit`
- `ProFaminstanceRetrieve`

## STEP

- `ProIntfimportSourceTypeGet`
- `ProIntfimportModelWithOptionsMdlnameCreate`

If a generated import model name already exists in session, STEP import retries with another `AVT_STEP_####` name.

## Inspection assembly

- `ProSolidOutlineGet`
- `ProAsmcompAssemble`

No constraint API is called after `ProAsmcompAssemble`, so components remain unconstrained.
