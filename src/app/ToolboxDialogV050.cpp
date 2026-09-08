#include "app/ToolboxDialog.h"

class LegacyToolboxDialog {
public:
    static ProError Show();
};

#define ToolboxDialog LegacyToolboxDialog
#include "ToolboxDialog.cpp"
#undef ToolboxDialog

namespace {
enum class V050View {
    ControlPanel,
    WeakDimensions,
    Accuracy,
    Inspection,
    InstanceBuilder
};

constexpr int kExitControlPanel = 5000;
constexpr int kExitWeak = 5001;
constexpr int kExitAccuracy = 5002;
constexpr int kExitInspection = 5003;
constexpr int kExitInstanceBuilder = 5004;
constexpr char kFullScreenButton[] = "FullScreenButton";
constexpr char kControlPanelButton[] = "ControlPanelButton";
constexpr char kInspectionUseZAxisV050[] = "InspUseZAxis";

const char* V050Resource(V050View view) {
    switch (view) {
    case V050View::WeakDimensions: return "aventics_weak";
    case V050View::Accuracy: return "aventics_accuracy";
    case V050View::Inspection: return "aventics_inspection";
    case V050View::InstanceBuilder: return "aventics_instance_builder";
    case V050View::ControlPanel:
    default: return "aventics_toolbox";
    }
}

int V050ExitCode(V050View view) {
    switch (view) {
    case V050View::WeakDimensions: return kExitWeak;
    case V050View::Accuracy: return kExitAccuracy;
    case V050View::Inspection: return kExitInspection;
    case V050View::InstanceBuilder: return kExitInstanceBuilder;
    case V050View::ControlPanel:
    default: return kExitControlPanel;
    }
}

bool V050ViewFromStatus(int status, V050View& view) {
    switch (status) {
    case kExitControlPanel: view = V050View::ControlPanel; return true;
    case kExitWeak: view = V050View::WeakDimensions; return true;
    case kExitAccuracy: view = V050View::Accuracy; return true;
    case kExitInspection: view = V050View::Inspection; return true;
    case kExitInstanceBuilder: view = V050View::InstanceBuilder; return true;
    default: return false;
    }
}

V050View g_v050View = V050View::ControlPanel;

void V050SaveVisibleState() {
    auto& context = AppContext::Instance();
    switch (g_v050View) {
    case V050View::WeakDimensions:
        context.weakFolder = UiUtils::GetInput(kDialog, kWeakFolder);
        context.weakSearch = UiUtils::GetInput(kDialog, kWeakSearch);
        context.weakUseSelection = UiUtils::GetCheck(kDialog, kWeakUseSelection, true);
        context.weakIssuesOnly = UiUtils::GetCheck(kDialog, kWeakIssuesOnly, false);
        context.weakRecursive = UiUtils::GetCheck(kDialog, kWeakRecursive, false);
        context.weakLatest = UiUtils::GetCheck(kDialog, kWeakLatest, true);
        break;
    case V050View::Accuracy:
        context.accuracyFolder = UiUtils::GetInput(kDialog, kAccFolder);
        context.accuracySearch = UiUtils::GetInput(kDialog, kAccSearch);
        context.accuracyUseSelection = UiUtils::GetCheck(kDialog, kAccUseSelection, true);
        context.accuracyIssuesOnly = UiUtils::GetCheck(kDialog, kAccIssuesOnly, false);
        context.accuracyRecursive = UiUtils::GetCheck(kDialog, kAccRecursive, false);
        context.accuracyLatest = UiUtils::GetCheck(kDialog, kAccLatest, true);
        context.accuracyParts = UiUtils::GetCheck(kDialog, kAccParts, true);
        context.accuracyAssemblies = UiUtils::GetCheck(kDialog, kAccAssemblies, true);
        break;
    case V050View::Inspection: {
        context.inspectionFolder = UiUtils::GetInput(kDialog, kInspFolder);
        context.inspectionRecursive = UiUtils::GetCheck(kDialog, kInspRecursive, false);
        context.inspectionLatest = UiUtils::GetCheck(kDialog, kInspLatest, true);
        context.inspectionParts = UiUtils::GetCheck(kDialog, kInspParts, true);
        context.inspectionAssemblies = UiUtils::GetCheck(kDialog, kInspAssemblies, true);
        context.inspectionFamilyInstances = UiUtils::GetCheck(kDialog, kInspFamily, true);
        context.inspectionIncludeGeneric = UiUtils::GetCheck(kDialog, kInspGeneric, false);
        context.inspectionStep = UiUtils::GetCheck(kDialog, kInspStep, true);
        context.inspectionAutoArrange = UiUtils::GetCheck(kDialog, kInspArrange, true);
        context.inspectionRowsAlongX = UiUtils::GetCheck(kDialog, kInspRowsAlongX, false);
        context.inspectionUseZAxis = UiUtils::GetCheck(kDialog, kInspectionUseZAxisV050, false);
        int columns = context.inspectionColumns;
        double gap = context.inspectionGap;
        if (ParsePositiveInt(UiUtils::GetInput(kDialog, kInspColumns), columns)) context.inspectionColumns = columns;
        if (ParseNonNegativeDouble(UiUtils::GetInput(kDialog, kInspGap), gap)) context.inspectionGap = gap;
        break;
    }
    case V050View::InstanceBuilder: {
        context.instanceFolder = UiUtils::GetInput(kDialog, "InstFolder");
        context.instanceCodes = UiUtils::GetTextArea(kDialog, "InstCodes");
        context.instanceRecursive = UiUtils::GetCheck(kDialog, "InstRecursive", false);
        context.instanceLatest = UiUtils::GetCheck(kDialog, "InstLatest", true);
        context.instanceUnresolvedOnly = UiUtils::GetCheck(kDialog, "InstUnresolvedOnly", false);
        int columns = context.instanceColumns;
        double gap = context.instanceGap;
        if (ParsePositiveInt(UiUtils::GetInput(kDialog, "InstColumns"), columns)) context.instanceColumns = columns;
        if (ParseNonNegativeDouble(UiUtils::GetInput(kDialog, "InstGap"), gap)) context.instanceGap = gap;
        break;
    }
    case V050View::ControlPanel:
        break;
    }
}

void V050RestoreVisibleState() {
    auto& context = AppContext::Instance();
    switch (g_v050View) {
    case V050View::WeakDimensions:
        UiUtils::SetInput(kDialog, kWeakFolder, context.weakFolder);
        UiUtils::SetInput(kDialog, kWeakSearch, context.weakSearch);
        UiUtils::SetCheck(kDialog, kWeakUseSelection, context.weakUseSelection);
        UiUtils::SetCheck(kDialog, kWeakIssuesOnly, context.weakIssuesOnly);
        UiUtils::SetCheck(kDialog, kWeakRecursive, context.weakRecursive);
        UiUtils::SetCheck(kDialog, kWeakLatest, context.weakLatest);
        UiUtils::SetProgress(kDialog, kWeakProgress, 0, 1);
        UiUtils::SetLabel(kDialog, kWeakFound, context.weakUseSelection
            ? L"Selection mode: choose parts after starting the check."
            : L"Folder mode: choose a folder and options, then start the check.");
        break;
    case V050View::Accuracy:
        UiUtils::SetInput(kDialog, kAccFolder, context.accuracyFolder);
        UiUtils::SetInput(kDialog, kAccSearch, context.accuracySearch);
        UiUtils::SetCheck(kDialog, kAccUseSelection, context.accuracyUseSelection);
        UiUtils::SetCheck(kDialog, kAccIssuesOnly, context.accuracyIssuesOnly);
        UiUtils::SetCheck(kDialog, kAccRecursive, context.accuracyRecursive);
        UiUtils::SetCheck(kDialog, kAccLatest, context.accuracyLatest);
        UiUtils::SetCheck(kDialog, kAccParts, context.accuracyParts);
        UiUtils::SetCheck(kDialog, kAccAssemblies, context.accuracyAssemblies);
        UiUtils::SetProgress(kDialog, kAccProgress, 0, 1);
        UiUtils::SetLabel(kDialog, kAccFound, context.accuracyUseSelection
            ? L"Selection mode: choose models after starting the check."
            : L"Folder mode: choose a folder and options, then start the check.");
        break;
    case V050View::Inspection:
        UiUtils::SetInput(kDialog, kInspFolder, context.inspectionFolder);
        UiUtils::SetCheck(kDialog, kInspRecursive, context.inspectionRecursive);
        UiUtils::SetCheck(kDialog, kInspLatest, context.inspectionLatest);
        UiUtils::SetCheck(kDialog, kInspParts, context.inspectionParts);
        UiUtils::SetCheck(kDialog, kInspAssemblies, context.inspectionAssemblies);
        UiUtils::SetCheck(kDialog, kInspFamily, context.inspectionFamilyInstances);
        UiUtils::SetCheck(kDialog, kInspGeneric, context.inspectionIncludeGeneric);
        UiUtils::SetCheck(kDialog, kInspStep, context.inspectionStep);
        UiUtils::SetCheck(kDialog, kInspArrange, context.inspectionAutoArrange);
        UiUtils::SetCheck(kDialog, kInspRowsAlongX, context.inspectionRowsAlongX);
        UiUtils::SetCheck(kDialog, kInspectionUseZAxisV050, context.inspectionUseZAxis);
        UiUtils::SetInput(kDialog, kInspColumns, std::to_wstring(context.inspectionColumns));
        UiUtils::SetInput(kDialog, kInspGap, DoubleText(context.inspectionGap));
        UiUtils::SetProgress(kDialog, kInspProgress, 0, 1);
        break;
    case V050View::InstanceBuilder:
        UiUtils::SetInput(kDialog, "InstFolder", context.instanceFolder);
        UiUtils::SetTextArea(kDialog, "InstCodes", context.instanceCodes);
        UiUtils::SetCheck(kDialog, "InstRecursive", context.instanceRecursive);
        UiUtils::SetCheck(kDialog, "InstLatest", context.instanceLatest);
        UiUtils::SetCheck(kDialog, "InstUnresolvedOnly", context.instanceUnresolvedOnly);
        UiUtils::SetInput(kDialog, "InstColumns", std::to_wstring(context.instanceColumns));
        UiUtils::SetInput(kDialog, "InstGap", DoubleText(context.instanceGap));
        UiUtils::SetProgress(kDialog, "InstProgress", 0, 1);
        break;
    case V050View::ControlPanel:
        UiUtils::SetLabel(kDialog, kOverviewRunAllStatus,
            L"Ready. Open a tool from the Control Panel to work in its own window.");
        break;
    }
}

void V050RefreshVisible() {
    switch (g_v050View) {
    case V050View::WeakDimensions: RefreshWeakTable(); break;
    case V050View::Accuracy: RefreshAccuracyTable(); break;
    case V050View::Inspection:
        RefreshInspectionTable();
        RefreshActiveAssembly();
        break;
    case V050View::InstanceBuilder:
        InstanceBuilderUi::Refresh(kDialog);
        break;
    case V050View::ControlPanel:
        break;
    }
}

void V050UpdateVisibleControls() {
    const bool running = OperationRunner::Instance().IsRunning();
    UiUtils::EnableButton(kDialog, kFooterCancel, running && !OperationRunner::Instance().IsCancelling());

    switch (g_v050View) {
    case V050View::ControlPanel:
        for (const char* button : {"OverviewRunAllButton", "OverviewWeakButton", "OverviewAccButton",
                                   "OverviewInspButton", "OverviewInstButton"})
            UiUtils::EnableButton(kDialog, button, !running);
        break;
    case V050View::WeakDimensions: {
        const bool selection = UiUtils::GetCheck(kDialog, kWeakUseSelection, true);
        UiUtils::EnableButton(kDialog, "WeakRunButton", !running);
        UiUtils::EnableButton(kDialog, "WeakClearButton", !running);
        UiUtils::EnableCheck(kDialog, kWeakUseSelection, !running);
        UiUtils::EnableInput(kDialog, kWeakFolder, !running && !selection);
        UiUtils::EnableButton(kDialog, "WeakBrowseButton", !running && !selection);
        UiUtils::EnableCheck(kDialog, kWeakRecursive, !running && !selection);
        UiUtils::EnableCheck(kDialog, kWeakLatest, !running && !selection);
        if (!running) RefreshWeakDetails();
        break;
    }
    case V050View::Accuracy: {
        const bool selection = UiUtils::GetCheck(kDialog, kAccUseSelection, true);
        UiUtils::EnableButton(kDialog, "AccRunButton", !running);
        UiUtils::EnableButton(kDialog, "AccClearButton", !running);
        UiUtils::EnableCheck(kDialog, kAccUseSelection, !running);
        UiUtils::EnableInput(kDialog, kAccFolder, !running && !selection);
        UiUtils::EnableButton(kDialog, "AccBrowseButton", !running && !selection);
        UiUtils::EnableCheck(kDialog, kAccRecursive, !running && !selection);
        UiUtils::EnableCheck(kDialog, kAccLatest, !running && !selection);
        UiUtils::EnableCheck(kDialog, kAccParts, !running);
        UiUtils::EnableCheck(kDialog, kAccAssemblies, !running);
        if (!running) RefreshAccuracyDetails();
        break;
    }
    case V050View::Inspection: {
        const bool autoArrange = UiUtils::GetCheck(kDialog, kInspArrange, true);
        UiUtils::EnableButton(kDialog, "InspBuildButton", !running);
        UiUtils::EnableButton(kDialog, "InspBrowseButton", !running);
        UiUtils::EnableButton(kDialog, "InspClearButton", !running);
        for (const char* check : {kInspRecursive, kInspLatest, kInspParts, kInspAssemblies,
                                  kInspFamily, kInspGeneric, kInspStep, kInspArrange})
            UiUtils::EnableCheck(kDialog, check, !running);
        UiUtils::EnableCheck(kDialog, kInspRowsAlongX, !running && autoArrange);
        UiUtils::EnableCheck(kDialog, kInspectionUseZAxisV050, !running && autoArrange);
        UiUtils::EnableInput(kDialog, kInspFolder, !running);
        UiUtils::EnableInput(kDialog, kInspColumns, !running && autoArrange);
        UiUtils::EnableInput(kDialog, kInspGap, !running && autoArrange);
        break;
    }
    case V050View::InstanceBuilder:
        InstanceBuilderUi::UpdateControls(kDialog, running);
        break;
    }

    if (g_v050View != V050View::ControlPanel) {
        UiUtils::EnableButton(kDialog, kFullScreenButton, true);
        UiUtils::EnableButton(kDialog, kControlPanelButton, true);
    }
}

void V050Navigate(V050View view) {
    if (OperationRunner::Instance().IsRunning()) {
        SetGlobalStatus(L"Finish or cancel the current operation before changing tools.");
        V050UpdateVisibleControls();
        return;
    }
    V050SaveVisibleState();
    ProUIDialogExit(const_cast<char*>(kDialog), V050ExitCode(view));
}

void V050OnWeak(char*, char*, ProAppData) { V050Navigate(V050View::WeakDimensions); }
void V050OnAccuracy(char*, char*, ProAppData) { V050Navigate(V050View::Accuracy); }
void V050OnInspection(char*, char*, ProAppData) { V050Navigate(V050View::Inspection); }
void V050OnInstance(char*, char*, ProAppData) { V050Navigate(V050View::InstanceBuilder); }
void V050OnControlPanel(char*, char*, ProAppData) { V050Navigate(V050View::ControlPanel); }

void V050OnFullScreen(char*, char*, ProAppData) {
    ProUIDialogHorzsizeSet(const_cast<char*>(kDialog), 1000);
    ProUIDialogVertsizeSet(const_cast<char*>(kDialog), 1000);
    SetGlobalStatus(L"Full screen enabled. Resize the window to leave full screen.");
}

void V050OnClose(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) {
        OnCancel(nullptr, nullptr, nullptr);
        SetGlobalStatus(L"Cancelling the current operation. Close again after it finishes.");
        return;
    }
    V050SaveVisibleState();
    ProUIDialogExit(const_cast<char*>(kDialog), PRO_TK_NO_ERROR);
}

void V050OnControlRunAll(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;

    std::vector<ModelDescriptor> models;
    ProUIDialogHide(const_cast<char*>(kDialog));
    const ProError selectError = SelectionService::SelectModels(true, true, models);
    ProUIDialogShow(const_cast<char*>(kDialog));
    if (selectError == PRO_TK_USER_ABORT) {
        SetGlobalStatus(L"Selection cancelled. Previous results were kept.");
        return;
    }
    if (selectError != PRO_TK_NO_ERROR || models.empty()) {
        const std::wstring message = selectError == PRO_TK_NO_ERROR
            ? L"No parts or assemblies selected."
            : L"Selection failed: " + ModelUtils::ErrorName(selectError);
        UiUtils::SetLabel(kDialog, kOverviewRunAllStatus, message);
        SetGlobalStatus(message);
        return;
    }

    auto& context = AppContext::Instance();
    context.runAllPending = std::move(models);
    context.weakResults.clear();
    context.accuracyResults.clear();
    g_operationName = L"Run all QC";

    const ProError startError = OperationRunner::Instance().Start(kDialog, context.runAllPending.size(),
        [](std::size_t index) {
            auto& state = AppContext::Instance();
            const auto& source = state.runAllPending[index];
            const std::wstring message = L"Run all QC | Checking " + std::to_wstring(index + 1) + L" / " +
                std::to_wstring(state.runAllPending.size()) + L": " + source.displayName;
            UiUtils::SetLabel(kDialog, kOverviewRunAllStatus, message);
            SetGlobalStatus(message);
            state.accuracyResults.push_back(AccuracyChecker().Run(source.model));
            if (source.modelType == PRO_MDL_PART)
                state.weakResults.push_back(WeakDimensionChecker().Run(source.model));
        },
        [](std::size_t done, std::size_t total) {
            SetGlobalStatus(L"Run all QC | " + std::to_wstring(done) + L" / " + std::to_wstring(total));
        },
        [](bool cancelled) {
            auto& state = AppContext::Instance();
            state.runAllPending.clear();
            g_operationName.clear();
            const std::wstring message = cancelled
                ? L"Run all QC cancelled. Partial results were kept."
                : L"Run all QC completed. Open Weak Dimensions or Accuracy from the Control Panel to review results.";
            UiUtils::SetLabel(kDialog, kOverviewRunAllStatus, message);
            SetGlobalStatus(message);
            V050UpdateVisibleControls();
        });

    if (startError != PRO_TK_NO_ERROR) {
        context.runAllPending.clear();
        g_operationName.clear();
        const std::wstring message = L"Could not start Run all QC: " + ModelUtils::ErrorName(startError);
        UiUtils::SetLabel(kDialog, kOverviewRunAllStatus, message);
        SetGlobalStatus(message);
        return;
    }

    V050UpdateVisibleControls();
}

void V050SetupCallbacks() {
    RegisterButton(kFooterCancel, OnCancel);
    RegisterButton("CloseButton", V050OnClose);
    ProUIDialogCloseActionSet(const_cast<char*>(kDialog), V050OnClose, nullptr);

    switch (g_v050View) {
    case V050View::ControlPanel:
        RegisterButton("OverviewWeakButton", V050OnWeak);
        RegisterButton("OverviewAccButton", V050OnAccuracy);
        RegisterButton("OverviewInspButton", V050OnInspection);
        RegisterButton("OverviewInstButton", V050OnInstance);
        RegisterButton("OverviewRunAllButton", V050OnControlRunAll);
        break;
    case V050View::WeakDimensions:
        RegisterButton(kFullScreenButton, V050OnFullScreen);
        RegisterButton(kControlPanelButton, V050OnControlPanel);
        RegisterButton("WeakBrowseButton", OnWeakBrowse);
        RegisterButton("WeakRunButton", OnWeakRun);
        RegisterButton("WeakClearButton", OnWeakClear);
        RegisterButton("WeakOpenButton", OnWeakOpen);
        RegisterButton("WeakGotoButton", OnWeakGoto);
        RegisterCheck(kWeakUseSelection, OnWeakSourceChanged);
        RegisterCheck(kWeakIssuesOnly, OnWeakFilter);
        for (const char* check : {kWeakRecursive, kWeakLatest}) RegisterCheck(check, OnWeakSetupChanged);
        RegisterInput(kWeakFolder, OnWeakSetupChanged);
        RegisterInput(kWeakSearch, OnWeakSearch);
        ProUITableSelectionpolicySet(const_cast<char*>(kDialog), const_cast<char*>(kWeakTable), PROUISELPOLICY_SINGLE);
        ProUITableSelectActionSet(const_cast<char*>(kDialog), const_cast<char*>(kWeakTable), OnWeakSelected, nullptr);
        break;
    case V050View::Accuracy:
        RegisterButton(kFullScreenButton, V050OnFullScreen);
        RegisterButton(kControlPanelButton, V050OnControlPanel);
        RegisterButton("AccBrowseButton", OnAccBrowse);
        RegisterButton("AccRunButton", OnAccuracyRun);
        RegisterButton("AccClearButton", OnAccClear);
        RegisterButton("AccOpenButton", OnAccOpen);
        RegisterCheck(kAccUseSelection, OnAccSourceChanged);
        RegisterCheck(kAccIssuesOnly, OnAccFilter);
        for (const char* check : {kAccRecursive, kAccLatest, kAccParts, kAccAssemblies})
            RegisterCheck(check, OnAccSetupChanged);
        RegisterInput(kAccFolder, OnAccSetupChanged);
        RegisterInput(kAccSearch, OnAccSearch);
        ProUITableSelectionpolicySet(const_cast<char*>(kDialog), const_cast<char*>(kAccTable), PROUISELPOLICY_SINGLE);
        ProUITableSelectActionSet(const_cast<char*>(kDialog), const_cast<char*>(kAccTable), OnAccSelected, nullptr);
        break;
    case V050View::Inspection:
        RegisterButton(kFullScreenButton, V050OnFullScreen);
        RegisterButton(kControlPanelButton, V050OnControlPanel);
        RegisterButton("InspBrowseButton", OnInspBrowse);
        RegisterButton("InspBuildButton", OnInspectionBuild);
        RegisterButton("InspClearButton", OnInspClear);
        for (const char* check : {kInspRecursive, kInspLatest, kInspParts, kInspAssemblies,
                                  kInspFamily, kInspGeneric, kInspStep, kInspArrange, kInspRowsAlongX,
                                  kInspectionUseZAxisV050})
            RegisterCheck(check, OnInspSetupChanged);
        RegisterInput(kInspFolder, OnInspSetupChanged);
        RegisterInput(kInspColumns, OnInspSetupChanged);
        RegisterInput(kInspGap, OnInspSetupChanged);
        ProUITableSelectionpolicySet(const_cast<char*>(kDialog), const_cast<char*>(kInspTable), PROUISELPOLICY_SINGLE);
        ProUITableSelectActionSet(const_cast<char*>(kDialog), const_cast<char*>(kInspTable), OnInspSelected, nullptr);
        break;
    case V050View::InstanceBuilder:
        RegisterButton(kFullScreenButton, V050OnFullScreen);
        RegisterButton(kControlPanelButton, V050OnControlPanel);
        InstanceBuilderUi::SetupCallbacks(kDialog);
        break;
    }
}

ProError V050ShowView(V050View view, int& status) {
    g_v050View = view;
    const ProError createError = ProUIDialogCreate(
        const_cast<char*>(kDialog),
        const_cast<char*>(V050Resource(view)));
    if (createError != PRO_TK_NO_ERROR) {
        Logger::Error(L"ProUIDialogCreate failed for v0.5.0 view: " + ModelUtils::ErrorName(createError));
        return createError;
    }

    V050SetupCallbacks();
    V050RestoreVisibleState();
    V050RefreshVisible();
    V050UpdateVisibleControls();

    status = PRO_TK_NO_ERROR;
    const ProError activateError = ProUIDialogActivate(const_cast<char*>(kDialog), &status);
    ProUIDialogDestroy(const_cast<char*>(kDialog));
    return activateError;
}
}

ProError ToolboxDialog::Show() {
    V050View view = V050View::ControlPanel;
    for (;;) {
        int status = PRO_TK_NO_ERROR;
        const ProError error = V050ShowView(view, status);
        if (error != PRO_TK_NO_ERROR) return error;

        V050View next = view;
        if (!V050ViewFromStatus(status, next)) return PRO_TK_NO_ERROR;
        view = next;
    }
}
