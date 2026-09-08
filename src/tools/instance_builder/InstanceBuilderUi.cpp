#include <ProToolkit.h>
#include "tools/InstanceBuilderUi.h"

#include "app/AppContext.h"
#include "common/FolderPicker.h"
#include "common/FolderScanner.h"
#include "common/ModelUtils.h"
#include "common/OperationRunner.h"
#include "common/UiUtils.h"
#include "tools/InstanceBuilder.h"

#include <ProMdl.h>
#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProUITextarea.h>
#include <ProUICheckbutton.h>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr char kDialog[] = "aventics_toolbox";
constexpr char kFolder[] = "InstFolder";
constexpr char kRecursive[] = "InstRecursive";
constexpr char kLatest[] = "InstLatest";
constexpr char kFound[] = "InstFound";
constexpr char kCodes[] = "InstCodes";
constexpr char kColumns[] = "InstColumns";
constexpr char kGap[] = "InstGap";
constexpr char kValidation[] = "InstValidation";
constexpr char kProgress[] = "InstProgress";
constexpr char kProgressText[] = "InstProgressText";
constexpr char kActiveAsm[] = "InstActiveAsm";
constexpr char kTable[] = "InstTable";
constexpr char kSummary[] = "InstSummary";
constexpr char kDetails[] = "InstDetails";
constexpr char kFooterStatus[] = "FooterStatus";

std::unique_ptr<InstanceBuilder> g_builder;

void SetGlobalStatus(const std::wstring& text) {
    UiUtils::SetLabel(kDialog, kFooterStatus, text);
}

bool ParsePositiveInt(const std::wstring& text, int& value) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(text, &used);
        if (used != text.size() || parsed < 1) return false;
        value = parsed;
        return true;
    } catch (...) { return false; }
}

bool ParseNonNegativeDouble(const std::wstring& text, double& value) {
    try {
        std::size_t used = 0;
        const double parsed = std::stod(text, &used);
        if (used != text.size() || !std::isfinite(parsed) || parsed < 0.0) return false;
        value = parsed;
        return true;
    } catch (...) { return false; }
}

bool ReadInputs(InstanceBuilderOptions& options,
                std::vector<InstanceRequest>& requests,
                std::wstring& error) {
    options.includeSubfolders = UiUtils::GetCheck(kDialog, kRecursive, false);
    options.latestCreoVersionOnly = UiUtils::GetCheck(kDialog, kLatest, true);
    if (!ParsePositiveInt(UiUtils::GetInput(kDialog, kColumns), options.columns)) {
        error = L"Columns must be a whole number of 1 or greater.";
        return false;
    }
    if (!ParseNonNegativeDouble(UiUtils::GetInput(kDialog, kGap), options.gap)) {
        error = L"Gap must be a finite number of 0 or greater.";
        return false;
    }
    return InstanceBuilder::ParseRequests(UiUtils::GetTextArea(kDialog, kCodes), options.columns, requests, error);
}

FolderScanOptions SourceOptions(const InstanceBuilderOptions& options) {
    FolderScanOptions scan;
    scan.includeSubfolders = options.includeSubfolders;
    scan.latestCreoVersionOnly = options.latestCreoVersionOnly;
    scan.includeParts = true;
    scan.includeAssemblies = true;
    scan.includeStep = false;
    return scan;
}

bool HasBuildResults() {
    for (const auto& result : AppContext::Instance().instanceResults) {
        if (result.status != InstanceBuildStatus::Planned) return true;
    }
    return false;
}

void RefreshActiveAssembly() {
    ProMdl current = nullptr;
    if (ModelUtils::CurrentModel(&current) == PRO_TK_NO_ERROR && ModelUtils::IsAssembly(current)) {
        UiUtils::SetLabel(kDialog, kActiveAsm,
            L"Active: " + ModelUtils::ModelName(current) + L" | Instances will be added unconstrained; no automatic save.");
    } else {
        UiUtils::SetLabel(kDialog, kActiveAsm,
            L"No active assembly. Open or create an assembly before building instances.");
    }
}

void RefreshDetails() {
    const int selected = UiUtils::SelectedRowIndex(kDialog, kTable, 'b');
    std::wstring detail = L"Select a row to see source and build details.";
    const auto& results = AppContext::Instance().instanceResults;
    if (selected >= 0 && static_cast<std::size_t>(selected) < results.size()) {
        const auto& result = results[static_cast<std::size_t>(selected)];
        detail = result.requestedCode + L" | row " + std::to_wstring(result.row) + L", column " +
                 std::to_wstring(result.column);
        if (!result.sourcePath.empty()) detail += L" | " + result.sourcePath;
        if (!result.details.empty()) detail += L" | " + result.details;
    }
    UiUtils::SetLabel(kDialog, kDetails, detail);
}

void RefreshTable() {
    const auto& results = AppContext::Instance().instanceResults;
    const int oldSelected = UiUtils::SelectedRowIndex(kDialog, kTable, 'b');
    std::vector<std::string> rows;
    for (std::size_t i = 0; i < results.size(); ++i) rows.push_back("b" + std::to_string(i));
    UiUtils::SetRows(kDialog, kTable, rows);
    if (oldSelected >= 0 && static_cast<std::size_t>(oldSelected) < results.size())
        UiUtils::SelectRow(kDialog, kTable, "b" + std::to_string(oldSelected));

    if (rows.empty()) {
        UiUtils::SetCell(kDialog, kTable, "empty", "Code", L"No plan");
        for (const char* column : {"Row", "Column", "Generic", "AddedModel", "Status", "FeatureId", "Details"})
            UiUtils::SetCell(kDialog, kTable, "empty", column, L"-");
        UiUtils::SetLabel(kDialog, kSummary, L"No plan");
        RefreshDetails();
        return;
    }

    int planned = 0, added = 0, notFound = 0, failed = 0, skipped = 0;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        const std::string row = "b" + std::to_string(i);
        UiUtils::SetCell(kDialog, kTable, row.c_str(), "Code", result.requestedCode);
        UiUtils::SetCell(kDialog, kTable, row.c_str(), "Row", std::to_wstring(result.row));
        UiUtils::SetCell(kDialog, kTable, row.c_str(), "Column", std::to_wstring(result.column));
        UiUtils::SetCell(kDialog, kTable, row.c_str(), "Generic", result.genericName.empty() ? L"-" : result.genericName);
        UiUtils::SetCell(kDialog, kTable, row.c_str(), "AddedModel", result.addedModelName.empty() ? L"-" : result.addedModelName);
        UiUtils::SetCell(kDialog, kTable, row.c_str(), "Status", InstanceBuilder::StatusName(result.status));
        UiUtils::SetCell(kDialog, kTable, row.c_str(), "FeatureId",
                         result.componentFeatureId >= 0 ? std::to_wstring(result.componentFeatureId) : L"-");
        UiUtils::SetCell(kDialog, kTable, row.c_str(), "Details", result.details.empty() ? L"-" : result.details);
        switch (result.status) {
        case InstanceBuildStatus::Planned: ++planned; break;
        case InstanceBuildStatus::Added: ++added; break;
        case InstanceBuildStatus::NotFound: ++notFound; break;
        case InstanceBuildStatus::Failed: ++failed; break;
        case InstanceBuildStatus::Skipped: ++skipped; break;
        }
    }

    std::wstringstream summary;
    summary << results.size() << L" requested";
    if (planned == static_cast<int>(results.size())) {
        int maxRow = 0;
        for (const auto& result : results) maxRow = std::max(maxRow, result.row);
        summary << L" | " << maxRow << L" rows planned";
    } else {
        summary << L" | " << added << L" added | " << notFound << L" not found | "
                << failed << L" failed | " << skipped << L" skipped";
    }
    UiUtils::SetLabel(kDialog, kSummary, summary.str());
    RefreshDetails();
}

void PlanFromInputs(bool announce) {
    InstanceBuilderOptions options;
    std::vector<InstanceRequest> requests;
    std::wstring error;
    if (!ReadInputs(options, requests, error)) {
        UiUtils::SetLabel(kDialog, kValidation, error);
        if (announce) SetGlobalStatus(error);
        return;
    }

    auto& context = AppContext::Instance();
    context.instanceResults.clear();
    context.instanceResults.reserve(requests.size());
    for (const auto& request : requests) {
        InstanceBuildResult result;
        result.requestedCode = request.code;
        result.row = request.row;
        result.column = request.column;
        result.status = InstanceBuildStatus::Planned;
        result.details = L"Allocated; exact instance not searched yet.";
        context.instanceResults.push_back(std::move(result));
    }
    UiUtils::SetLabel(kDialog, kValidation, L"");
    RefreshTable();
    if (announce) SetGlobalStatus(L"Instance positions planned. Review row/column allocation before building.");
}

void OnBrowse(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    auto& context = AppContext::Instance();
    std::wstring picked;
    const std::wstring current = UiUtils::GetInput(kDialog, kFolder);
    if (!FolderPicker::Pick(L"Select folder containing Creo generics for Instance Builder",
                            current.empty() ? context.instanceFolder : current, picked)) return;
    context.instanceFolder = picked;
    UiUtils::SetInput(kDialog, kFolder, picked);
    UiUtils::SetLabel(kDialog, kFound, L"Folder selected. Build will search Creo parts and assemblies for exact instances.");
}

void OnPlan(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    PlanFromInputs(true);
}

void OnBuild(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;

    ProMdl current = nullptr;
    if (ModelUtils::CurrentModel(&current) != PRO_TK_NO_ERROR || !ModelUtils::IsAssembly(current)) {
        RefreshActiveAssembly();
        SetGlobalStatus(L"Open or create an assembly before building instances.");
        return;
    }

    InstanceBuilderOptions options;
    std::vector<InstanceRequest> requests;
    std::wstring error;
    if (!ReadInputs(options, requests, error)) {
        UiUtils::SetLabel(kDialog, kValidation, error);
        SetGlobalStatus(error);
        return;
    }

    auto& context = AppContext::Instance();
    context.instanceFolder = UiUtils::GetInput(kDialog, kFolder);
    if (context.instanceFolder.empty()) {
        const std::wstring message = L"Choose a source folder before building instances.";
        UiUtils::SetLabel(kDialog, kValidation, message);
        SetGlobalStatus(message);
        return;
    }

    const auto scan = FolderScanner::Discover(context.instanceFolder, SourceOptions(options));
    if (!scan.error.empty() || scan.models.empty()) {
        const std::wstring message = scan.error.empty()
            ? L"No Creo parts or assemblies were found in the selected folder."
            : scan.error;
        UiUtils::SetLabel(kDialog, kFound, message);
        SetGlobalStatus(message);
        return;
    }

    // Refresh the logical grid immediately; the build will preserve these cells.
    context.instanceResults.clear();
    for (const auto& request : requests) {
        InstanceBuildResult result;
        result.requestedCode = request.code;
        result.row = request.row;
        result.column = request.column;
        result.status = InstanceBuildStatus::Planned;
        result.details = L"Searching...";
        context.instanceResults.push_back(std::move(result));
    }
    RefreshTable();

    context.instancePending = scan.models;
    g_builder = std::make_unique<InstanceBuilder>(reinterpret_cast<ProAssembly>(current), options, std::move(requests));
    UiUtils::SetLabel(kDialog, kValidation, L"");
    UiUtils::SetLabel(kDialog, kFound, L"Searching " + std::to_wstring(scan.models.size()) + L" Creo source models.");

    const ProError startError = OperationRunner::Instance().Start(kDialog, context.instancePending.size(),
        [](std::size_t index) {
            auto& state = AppContext::Instance();
            const auto& source = state.instancePending[index];
            const std::wstring message = L"Instance Builder | Searching: " + source.displayName;
            UiUtils::SetLabel(kDialog, kProgressText, message);
            SetGlobalStatus(message);
            if (g_builder) g_builder->ProcessSource(source);
        },
        [](std::size_t done, std::size_t total) {
            UiUtils::SetProgress(kDialog, kProgress, static_cast<int>(done), static_cast<int>(total));
            const std::wstring resolved = g_builder ? std::to_wstring(g_builder->ResolvedCount()) : L"0";
            const std::wstring message = L"Instance Builder | " + std::to_wstring(done) + L" / " +
                std::to_wstring(total) + L" source models | " + resolved + L" requested entries resolved";
            UiUtils::SetLabel(kDialog, kProgressText, message);
            SetGlobalStatus(message);
        },
        [](bool cancelled) {
            auto& state = AppContext::Instance();
            if (cancelled) {
                const std::wstring message = L"Instance Builder cancelled before assembly. No new instance components were added.";
                UiUtils::SetLabel(kDialog, kProgressText, message);
                state.instancePending.clear();
                g_builder.reset();
                SetGlobalStatus(message);
                InstanceBuilderUi::Refresh(kDialog);
                InstanceBuilderUi::UpdateControls(kDialog, false);
                return;
            }

            if (g_builder) g_builder->Finalize(state.instanceResults);
            state.instancePending.clear();
            g_builder.reset();
            const std::wstring message = L"Instance Builder completed. Review results and export the CSV report if needed.";
            UiUtils::SetLabel(kDialog, kProgressText, message);
            SetGlobalStatus(message);
            InstanceBuilderUi::Refresh(kDialog);
            InstanceBuilderUi::UpdateControls(kDialog, false);
        });

    if (startError != PRO_TK_NO_ERROR) {
        context.instancePending.clear();
        g_builder.reset();
        const std::wstring message = L"Could not start Instance Builder: " + ModelUtils::ErrorName(startError);
        UiUtils::SetLabel(kDialog, kProgressText, message);
        SetGlobalStatus(message);
        return;
    }

    UiUtils::SetProgress(kDialog, kProgress, 0, static_cast<int>(context.instancePending.size()));
    InstanceBuilderUi::UpdateControls(kDialog, true);
}

void OnExport(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    std::wstring message;
    InstanceBuilder::ExportCsv(AppContext::Instance().instanceResults, message);
    SetGlobalStatus(message);
    UiUtils::SetLabel(kDialog, kDetails, message);
}

void OnClear(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    AppContext::Instance().instanceResults.clear();
    UiUtils::SetProgress(kDialog, kProgress, 0, 1);
    UiUtils::SetLabel(kDialog, kProgressText, L"Ready");
    UiUtils::SetLabel(kDialog, kValidation, L"");
    RefreshTable();
    InstanceBuilderUi::UpdateControls(kDialog, false);
}

void OnSetupChanged(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    UiUtils::SetLabel(kDialog, kValidation, L"Inputs changed. Plan positions again before building.");
}

void OnSelected(char*, char*, ProAppData) {
    RefreshDetails();
}

void RegisterButton(const char* name, ProUIAction action) {
    ProUIPushbuttonActivateActionSet(const_cast<char*>(kDialog), const_cast<char*>(name), action, nullptr);
}
}

namespace InstanceBuilderUi {
void SetupCallbacks(const char* dialog) {
    (void)dialog;
    RegisterButton("InstBrowseButton", OnBrowse);
    RegisterButton("InstPlanButton", OnPlan);
    RegisterButton("InstBuildButton", OnBuild);
    RegisterButton("InstExportButton", OnExport);
    RegisterButton("InstClearButton", OnClear);
    ProUICheckbuttonActivateActionSet(const_cast<char*>(kDialog), const_cast<char*>(kRecursive), OnSetupChanged, nullptr);
    ProUICheckbuttonActivateActionSet(const_cast<char*>(kDialog), const_cast<char*>(kLatest), OnSetupChanged, nullptr);
    ProUIInputpanelInputActionSet(const_cast<char*>(kDialog), const_cast<char*>(kFolder), OnSetupChanged, nullptr);
    ProUIInputpanelInputActionSet(const_cast<char*>(kDialog), const_cast<char*>(kColumns), OnSetupChanged, nullptr);
    ProUIInputpanelInputActionSet(const_cast<char*>(kDialog), const_cast<char*>(kGap), OnSetupChanged, nullptr);
    ProUITextareaInputActionSet(const_cast<char*>(kDialog), const_cast<char*>(kCodes), OnSetupChanged, nullptr);
    ProUITableSelectionpolicySet(const_cast<char*>(kDialog), const_cast<char*>(kTable), PROUISELPOLICY_SINGLE);
    ProUITableSelectActionSet(const_cast<char*>(kDialog), const_cast<char*>(kTable), OnSelected, nullptr);
}

void SaveState(const char* dialog) {
    (void)dialog;
    auto& context = AppContext::Instance();
    context.instanceFolder = UiUtils::GetInput(kDialog, kFolder);
    context.instanceCodes = UiUtils::GetTextArea(kDialog, kCodes);
    context.instanceRecursive = UiUtils::GetCheck(kDialog, kRecursive, false);
    context.instanceLatest = UiUtils::GetCheck(kDialog, kLatest, true);
    int columns = context.instanceColumns;
    double gap = context.instanceGap;
    if (ParsePositiveInt(UiUtils::GetInput(kDialog, kColumns), columns)) context.instanceColumns = columns;
    if (ParseNonNegativeDouble(UiUtils::GetInput(kDialog, kGap), gap)) context.instanceGap = gap;
}

void RestoreState(const char* dialog) {
    (void)dialog;
    auto& context = AppContext::Instance();
    UiUtils::SetInput(kDialog, kFolder, context.instanceFolder);
    UiUtils::SetTextArea(kDialog, kCodes, context.instanceCodes);
    UiUtils::SetCheck(kDialog, kRecursive, context.instanceRecursive);
    UiUtils::SetCheck(kDialog, kLatest, context.instanceLatest);
    UiUtils::SetInput(kDialog, kColumns, std::to_wstring(context.instanceColumns));
    std::wstringstream gap;
    gap.precision(12);
    gap << context.instanceGap;
    UiUtils::SetInput(kDialog, kGap, gap.str());
    UiUtils::SetProgress(kDialog, kProgress, 0, 1);
    UiUtils::SetLabel(kDialog, kProgressText, L"Ready");
    UiUtils::SetLabel(kDialog, kValidation, L"");
}

void Refresh(const char* dialog) {
    (void)dialog;
    RefreshActiveAssembly();
    RefreshTable();
}

void UpdateControls(const char* dialog, bool running) {
    (void)dialog;
    UiUtils::EnableButton(kDialog, "InstBrowseButton", !running);
    UiUtils::EnableButton(kDialog, "InstPlanButton", !running);
    UiUtils::EnableButton(kDialog, "InstBuildButton", !running);
    UiUtils::EnableButton(kDialog, "InstClearButton", !running);
    UiUtils::EnableButton(kDialog, "InstExportButton", !running && HasBuildResults());
    UiUtils::EnableInput(kDialog, kFolder, !running);
    UiUtils::EnableTextArea(kDialog, kCodes, !running);
    UiUtils::EnableInput(kDialog, kColumns, !running);
    UiUtils::EnableInput(kDialog, kGap, !running);
    UiUtils::EnableCheck(kDialog, kRecursive, !running);
    UiUtils::EnableCheck(kDialog, kLatest, !running);
    if (!running) RefreshDetails();
}
}
