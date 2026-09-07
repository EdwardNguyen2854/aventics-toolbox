#include <ProToolkit.h>
#include "app/ToolboxDialog.h"
#include "app/AppContext.h"
#include "common/FolderPicker.h"
#include "common/FolderScanner.h"
#include "common/Logger.h"
#include "common/ModelLoader.h"
#include "common/ModelUtils.h"
#include "common/OperationRunner.h"
#include "common/SelectionService.h"
#include "common/SessionSnapshot.h"
#include "common/UiUtils.h"
#include "tools/AccuracyChecker.h"
#include "tools/InspectionBuilder.h"
#include "tools/WeakDimensionChecker.h"

#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProUITab.h>
#include <ProUICheckbutton.h>
#include <ProUtil.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr char kDialog[] = "aventics_toolbox";
constexpr char kTabs[] = "ToolTabs";
constexpr char kFooterStatus[] = "FooterStatus";
constexpr char kFooterCancel[] = "FooterCancelButton";
constexpr char kOverviewRunAllStatus[] = "OverviewRunAllStatus";

constexpr char kWeakUseSelection[] = "WeakUseSelection";
constexpr char kWeakFolder[] = "WeakFolder";
constexpr char kWeakRecursive[] = "WeakRecursive";
constexpr char kWeakLatest[] = "WeakLatest";
constexpr char kWeakFound[] = "WeakFound";
constexpr char kWeakProgress[] = "WeakProgress";
constexpr char kWeakProgressText[] = "WeakProgressText";
constexpr char kWeakSearch[] = "WeakSearch";
constexpr char kWeakIssuesOnly[] = "WeakIssuesOnly";
constexpr char kWeakTable[] = "WeakTable";
constexpr char kWeakSummary[] = "WeakSummary";
constexpr char kWeakDetails[] = "WeakDetails";

constexpr char kAccUseSelection[] = "AccUseSelection";
constexpr char kAccFolder[] = "AccFolder";
constexpr char kAccRecursive[] = "AccRecursive";
constexpr char kAccLatest[] = "AccLatest";
constexpr char kAccParts[] = "AccParts";
constexpr char kAccAssemblies[] = "AccAssemblies";
constexpr char kAccFound[] = "AccFound";
constexpr char kAccProgress[] = "AccProgress";
constexpr char kAccProgressText[] = "AccProgressText";
constexpr char kAccSearch[] = "AccSearch";
constexpr char kAccIssuesOnly[] = "AccIssuesOnly";
constexpr char kAccTable[] = "AccTable";
constexpr char kAccSummary[] = "AccSummary";
constexpr char kAccDetails[] = "AccDetails";

constexpr char kInspFolder[] = "InspFolder";
constexpr char kInspRecursive[] = "InspRecursive";
constexpr char kInspLatest[] = "InspLatest";
constexpr char kInspParts[] = "InspParts";
constexpr char kInspAssemblies[] = "InspAssemblies";
constexpr char kInspFamily[] = "InspFamily";
constexpr char kInspGeneric[] = "InspGeneric";
constexpr char kInspStep[] = "InspStep";
constexpr char kInspArrange[] = "InspArrange";
constexpr char kInspRowsAlongX[] = "InspRowsAlongX";
constexpr char kInspColumns[] = "InspColumns";
constexpr char kInspGap[] = "InspGap";
constexpr char kInspActiveAsm[] = "InspActiveAsm";
constexpr char kInspFound[] = "InspFound";
constexpr char kInspValidation[] = "InspValidation";
constexpr char kInspProgress[] = "InspProgress";
constexpr char kInspProgressText[] = "InspProgressText";
constexpr char kInspTable[] = "InspTable";
constexpr char kInspSummary[] = "InspSummary";
constexpr char kInspDetails[] = "InspDetails";

struct WeakVisibleRow { std::size_t result = 0; int finding = -1; };
std::vector<WeakVisibleRow> g_weakVisible;
std::vector<std::size_t> g_accuracyVisible;
std::unique_ptr<InspectionBuilder> g_inspectionBuilder;
bool g_weakCleanup = false;
bool g_accuracyCleanup = false;
std::wstring g_operationName;

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool Contains(const std::wstring& value, const std::wstring& query) {
    return query.empty() || Lower(value).find(Lower(query)) != std::wstring::npos;
}

bool IsIssue(QcStatus status) {
    return status == QcStatus::Fail || status == QcStatus::Warning || status == QcStatus::Error;
}

std::wstring IntText(int value) { return value < 0 ? L"-" : std::to_wstring(value); }
std::wstring DimText(int value) { return value < 0 ? L"-" : L"sd" + std::to_wstring(value); }

std::wstring DoubleText(double value) {
    std::wstringstream ss;
    ss.precision(12);
    ss << value;
    return ss.str();
}

void SetGlobalStatus(const std::wstring& text) {
    UiUtils::SetLabel(kDialog, kFooterStatus, text);
}

void UpdateControls();

void SwitchTab(const char* layout) {
    char* values[] = { const_cast<char*>(layout) };
    ProUITabSelectednamesSet(const_cast<char*>(kDialog), const_cast<char*>(kTabs), 1, values);
}

void OnOverviewWeak(char*, char*, ProAppData) { SwitchTab("WeakLayout"); }
void OnOverviewAccuracy(char*, char*, ProAppData) { SwitchTab("AccuracyLayout"); }
void OnOverviewInspection(char*, char*, ProAppData) { SwitchTab("InspectionLayout"); }

void RefreshActiveAssembly() {
    ProMdl current = nullptr;
    if (ModelUtils::CurrentModel(&current) == PRO_TK_NO_ERROR && ModelUtils::IsAssembly(current)) {
        UiUtils::SetLabel(kDialog, kInspActiveAsm,
            L"Active: " + ModelUtils::ModelName(current) + L" | Components will be added; no automatic save.");
    } else {
        UiUtils::SetLabel(kDialog, kInspActiveAsm,
            L"No active assembly. Open or create an assembly before adding components.");
    }
}

FolderScanOptions WeakFolderOptions() {
    FolderScanOptions options;
    options.includeSubfolders = UiUtils::GetCheck(kDialog, kWeakRecursive, false);
    options.latestCreoVersionOnly = UiUtils::GetCheck(kDialog, kWeakLatest, true);
    options.includeParts = true;
    options.includeAssemblies = false;
    options.includeStep = false;
    return options;
}

FolderScanOptions AccuracyFolderOptions() {
    FolderScanOptions options;
    options.includeSubfolders = UiUtils::GetCheck(kDialog, kAccRecursive, false);
    options.latestCreoVersionOnly = UiUtils::GetCheck(kDialog, kAccLatest, true);
    options.includeParts = UiUtils::GetCheck(kDialog, kAccParts, true);
    options.includeAssemblies = UiUtils::GetCheck(kDialog, kAccAssemblies, true);
    options.includeStep = false;
    return options;
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

bool ReadInspectionOptions(InspectionOptions& options, std::wstring& error) {
    options.includeSubfolders = UiUtils::GetCheck(kDialog, kInspRecursive, false);
    options.latestCreoVersionOnly = UiUtils::GetCheck(kDialog, kInspLatest, true);
    options.includeParts = UiUtils::GetCheck(kDialog, kInspParts, true);
    options.includeAssemblies = UiUtils::GetCheck(kDialog, kInspAssemblies, true);
    options.includeFamilyInstances = UiUtils::GetCheck(kDialog, kInspFamily, true);
    options.includeGenericWhenFamilyExists = UiUtils::GetCheck(kDialog, kInspGeneric, false);
    options.includeStep = UiUtils::GetCheck(kDialog, kInspStep, true);
    options.autoArrange = UiUtils::GetCheck(kDialog, kInspArrange, true);
    options.arrangeRowsAlongX = UiUtils::GetCheck(kDialog, kInspRowsAlongX, false);
    if (!options.includeParts && !options.includeAssemblies && !options.includeStep) {
        error = L"Select at least one source type: Creo parts, assemblies, or STEP.";
        return false;
    }
    if (options.autoArrange) {
        if (!ParsePositiveInt(UiUtils::GetInput(kDialog, kInspColumns), options.columns)) {
            error = L"Columns must be a whole number of 1 or greater.";
            return false;
        }
        if (!ParseNonNegativeDouble(UiUtils::GetInput(kDialog, kInspGap), options.gap)) {
            error = L"Gap must be a finite number of 0 or greater.";
            return false;
        }
    }
    error.clear();
    return true;
}

FolderScanOptions InspectionFolderOptions(const InspectionOptions& input) {
    FolderScanOptions options;
    options.includeSubfolders = input.includeSubfolders;
    options.latestCreoVersionOnly = input.latestCreoVersionOnly;
    options.includeParts = input.includeParts;
    options.includeAssemblies = input.includeAssemblies;
    options.includeStep = input.includeStep;
    return options;
}

void RefreshWeakDetails() {
    const int selected = UiUtils::SelectedRowIndex(kDialog, kWeakTable, 'w');
    bool canOpen = false;
    bool canGoto = false;
    std::wstring detail = L"Select a result to see its source and technical details.";
    if (selected >= 0 && static_cast<std::size_t>(selected) < g_weakVisible.size()) {
        const auto visible = g_weakVisible[static_cast<std::size_t>(selected)];
        const auto& result = AppContext::Instance().weakResults[visible.result];
        detail = result.sourcePath.empty() ? result.modelName : result.sourcePath;
        if (visible.finding >= 0) {
            const auto& finding = result.findings[static_cast<std::size_t>(visible.finding)];
            detail += L" | " + finding.details;
            canGoto = finding.featureId >= 0;
        } else if (!result.summary.empty()) {
            detail += L" | " + result.summary;
        }
        canOpen = result.model != nullptr || !result.sourcePath.empty();
    }
    UiUtils::SetLabel(kDialog, kWeakDetails, detail);
    UiUtils::EnableButton(kDialog, "WeakOpenButton", canOpen);
    UiUtils::EnableButton(kDialog, "WeakGotoButton", canGoto);
}

void RefreshWeakTable() {
    auto& context = AppContext::Instance();
    const auto& results = context.weakResults;
    const std::wstring query = context.weakSearch;
    const int oldSelected = UiUtils::SelectedRowIndex(kDialog, kWeakTable, 'w');
    WeakVisibleRow oldVisible;
    const bool hadSelection = oldSelected >= 0 && static_cast<std::size_t>(oldSelected) < g_weakVisible.size();
    if (hadSelection) oldVisible = g_weakVisible[static_cast<std::size_t>(oldSelected)];
    g_weakVisible.clear();
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        if (result.findings.empty()) {
            const bool matches = Contains(result.modelName + L" " + result.sourcePath + L" " + result.summary, query);
            if (matches && (!context.weakIssuesOnly || IsIssue(result.status))) g_weakVisible.push_back({i, -1});
        } else {
            for (std::size_t j = 0; j < result.findings.size(); ++j) {
                const auto& finding = result.findings[j];
                const bool matches = Contains(result.modelName + L" " + result.sourcePath + L" " +
                    finding.featureName + L" " + finding.details, query);
                if (matches && (!context.weakIssuesOnly || IsIssue(finding.status)))
                    g_weakVisible.push_back({i, static_cast<int>(j)});
            }
        }
    }
    std::vector<std::string> rows;
    for (std::size_t i = 0; i < g_weakVisible.size(); ++i) rows.push_back("w" + std::to_string(i));
    UiUtils::SetRows(kDialog, kWeakTable, rows);
    if (hadSelection) {
        for (std::size_t i = 0; i < g_weakVisible.size(); ++i) {
            if (g_weakVisible[i].result == oldVisible.result && g_weakVisible[i].finding == oldVisible.finding) {
                UiUtils::SelectRow(kDialog, kWeakTable, "w" + std::to_string(i));
                break;
            }
        }
    }
    if (rows.empty()) {
        UiUtils::SetCell(kDialog, kWeakTable, "empty", "Part", results.empty() ? L"No results" : L"No matching results");
        for (const char* column : {"Status", "Feature", "Section", "Dimension", "Details"})
            UiUtils::SetCell(kDialog, kWeakTable, "empty", column, L"-");
    } else {
        for (std::size_t i = 0; i < g_weakVisible.size(); ++i) {
            const auto visible = g_weakVisible[i];
            const auto& result = results[visible.result];
            const std::string row = "w" + std::to_string(i);
            UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), "Part", result.modelName);
            if (visible.finding >= 0) {
                const auto& finding = result.findings[static_cast<std::size_t>(visible.finding)];
                std::wstring feature = finding.featureName;
                if (finding.featureId >= 0) feature += L" #" + std::to_wstring(finding.featureId);
                UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), "Status", ModelUtils::StatusName(finding.status));
                UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), "Feature", feature);
                UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), "Section", IntText(finding.sectionIndex));
                UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), "Dimension", DimText(finding.dimensionId));
                UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), "Details", finding.details);
            } else {
                UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), "Status", ModelUtils::StatusName(result.status));
                for (const char* column : {"Feature", "Section", "Dimension"})
                    UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), column, L"-");
                UiUtils::SetCell(kDialog, kWeakTable, row.c_str(), "Details", result.summary);
            }
        }
    }
    int pass = 0, fail = 0, warning = 0, error = 0, weak = 0;
    for (const auto& result : results) {
        weak += result.weakDimensions;
        if (result.status == QcStatus::Pass) ++pass;
        else if (result.status == QcStatus::Fail) ++fail;
        else if (result.status == QcStatus::Warning) ++warning;
        else if (result.status == QcStatus::Error) ++error;
    }
    std::wstringstream summary;
    summary << results.size() << L" parts | " << pass << L" pass | " << fail << L" fail | "
            << warning << L" warning | " << error << L" error | " << weak << L" weak dimensions";
    if (g_weakVisible.size() != results.size() || !query.empty() || context.weakIssuesOnly)
        summary << L" | " << g_weakVisible.size() << L" rows shown";
    UiUtils::SetLabel(kDialog, kWeakSummary, summary.str());
    RefreshWeakDetails();
}

void RefreshAccuracyDetails() {
    const int selected = UiUtils::SelectedRowIndex(kDialog, kAccTable, 'a');
    bool canOpen = false;
    std::wstring detail = L"Select a result to see its source and technical details.";
    if (selected >= 0 && static_cast<std::size_t>(selected) < g_accuracyVisible.size()) {
        const auto& result = AppContext::Instance().accuracyResults[g_accuracyVisible[static_cast<std::size_t>(selected)]];
        detail = result.sourcePath.empty() ? result.modelName : result.sourcePath;
        if (!result.details.empty()) detail += L" | " + result.details;
        canOpen = result.model != nullptr || !result.sourcePath.empty();
    }
    UiUtils::SetLabel(kDialog, kAccDetails, detail);
    UiUtils::EnableButton(kDialog, "AccOpenButton", canOpen);
}

void RefreshAccuracyTable() {
    auto& context = AppContext::Instance();
    const auto& results = context.accuracyResults;
    const int oldSelected = UiUtils::SelectedRowIndex(kDialog, kAccTable, 'a');
    std::size_t oldResult = 0;
    const bool hadSelection = oldSelected >= 0 && static_cast<std::size_t>(oldSelected) < g_accuracyVisible.size();
    if (hadSelection) oldResult = g_accuracyVisible[static_cast<std::size_t>(oldSelected)];
    g_accuracyVisible.clear();
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        const bool matches = Contains(result.modelName + L" " + result.sourcePath + L" " +
            result.accuracyType + L" " + result.details, context.accuracySearch);
        if (matches && (!context.accuracyIssuesOnly || IsIssue(result.status))) g_accuracyVisible.push_back(i);
    }
    std::vector<std::string> rows;
    for (std::size_t i = 0; i < g_accuracyVisible.size(); ++i) rows.push_back("a" + std::to_string(i));
    UiUtils::SetRows(kDialog, kAccTable, rows);
    if (hadSelection) {
        const auto found = std::find(g_accuracyVisible.begin(), g_accuracyVisible.end(), oldResult);
        if (found != g_accuracyVisible.end())
            UiUtils::SelectRow(kDialog, kAccTable, "a" + std::to_string(std::distance(g_accuracyVisible.begin(), found)));
    }
    if (rows.empty()) {
        UiUtils::SetCell(kDialog, kAccTable, "empty", "Model", results.empty() ? L"No results" : L"No matching results");
        for (const char* column : {"Kind", "AccuracyType", "Accuracy", "Status", "Details"})
            UiUtils::SetCell(kDialog, kAccTable, "empty", column, L"-");
    } else {
        for (std::size_t visible = 0; visible < g_accuracyVisible.size(); ++visible) {
            const auto& result = results[g_accuracyVisible[visible]];
            const std::string row = "a" + std::to_string(visible);
            UiUtils::SetCell(kDialog, kAccTable, row.c_str(), "Model", result.modelName);
            UiUtils::SetCell(kDialog, kAccTable, row.c_str(), "Kind", ModelUtils::ModelTypeName(result.modelType));
            UiUtils::SetCell(kDialog, kAccTable, row.c_str(), "AccuracyType", result.accuracyType.empty() ? L"-" : result.accuracyType);
            UiUtils::SetCell(kDialog, kAccTable, row.c_str(), "Accuracy", result.hasValue ? DoubleText(result.accuracyValue) : L"-");
            UiUtils::SetCell(kDialog, kAccTable, row.c_str(), "Status", ModelUtils::StatusName(result.status));
            UiUtils::SetCell(kDialog, kAccTable, row.c_str(), "Details", result.details);
        }
    }
    int pass = 0, fail = 0, warning = 0, error = 0, skip = 0;
    for (const auto& result : results) {
        if (result.status == QcStatus::Pass) ++pass;
        else if (result.status == QcStatus::Fail) ++fail;
        else if (result.status == QcStatus::Warning) ++warning;
        else if (result.status == QcStatus::Error) ++error;
        else if (result.status == QcStatus::Skip) ++skip;
    }
    std::wstringstream summary;
    summary << results.size() << L" models | " << pass << L" pass | " << fail << L" fail | "
            << warning << L" warning | " << error << L" error | " << skip << L" skipped";
    if (g_accuracyVisible.size() != results.size()) summary << L" | " << g_accuracyVisible.size() << L" shown";
    UiUtils::SetLabel(kDialog, kAccSummary, summary.str());
    RefreshAccuracyDetails();
}

void RefreshInspectionDetails() {
    const int selected = UiUtils::SelectedRowIndex(kDialog, kInspTable, 'i');
    std::wstring detail = L"Select a result to see its source and technical details.";
    if (selected >= 0 && static_cast<std::size_t>(selected) < AppContext::Instance().inspectionResults.size()) {
        const auto& result = AppContext::Instance().inspectionResults[static_cast<std::size_t>(selected)];
        detail = result.sourcePath.empty() ? result.sourceName : result.sourcePath;
        if (!result.details.empty()) detail += L" | " + result.details;
    }
    UiUtils::SetLabel(kDialog, kInspDetails, detail);
}

void RefreshInspectionTable() {
    const auto& results = AppContext::Instance().inspectionResults;
    const int oldSelected = UiUtils::SelectedRowIndex(kDialog, kInspTable, 'i');
    std::vector<std::string> rows;
    for (std::size_t i = 0; i < results.size(); ++i) rows.push_back("i" + std::to_string(i));
    UiUtils::SetRows(kDialog, kInspTable, rows);
    if (oldSelected >= 0 && static_cast<std::size_t>(oldSelected) < results.size())
        UiUtils::SelectRow(kDialog, kInspTable, "i" + std::to_string(oldSelected));
    if (rows.empty()) {
        UiUtils::SetCell(kDialog, kInspTable, "empty", "Source", L"No results");
        for (const char* column : {"Kind", "AddedModel", "Status", "FeatureId", "Details"})
            UiUtils::SetCell(kDialog, kInspTable, "empty", column, L"-");
    } else {
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            const std::string row = "i" + std::to_string(i);
            UiUtils::SetCell(kDialog, kInspTable, row.c_str(), "Source", result.sourceName);
            UiUtils::SetCell(kDialog, kInspTable, row.c_str(), "Kind", result.sourceKind);
            UiUtils::SetCell(kDialog, kInspTable, row.c_str(), "AddedModel", result.addedModelName.empty() ? L"-" : result.addedModelName);
            UiUtils::SetCell(kDialog, kInspTable, row.c_str(), "Status", ModelUtils::InspectionStatusName(result.status));
            UiUtils::SetCell(kDialog, kInspTable, row.c_str(), "FeatureId", IntText(result.componentFeatureId));
            UiUtils::SetCell(kDialog, kInspTable, row.c_str(), "Details", result.details);
        }
    }
    int added = 0, warning = 0, failed = 0, skipped = 0;
    for (const auto& result : results) {
        if (result.status == InspectionStatus::Added) ++added;
        else if (result.status == InspectionStatus::Warning) ++warning;
        else if (result.status == InspectionStatus::Failed) ++failed;
        else ++skipped;
    }
    std::wstringstream summary;
    summary << results.size() << L" results | " << added << L" added | " << warning << L" warning | "
            << failed << L" failed | " << skipped << L" skipped";
    UiUtils::SetLabel(kDialog, kInspSummary, summary.str());
    RefreshInspectionDetails();
}

void RefreshAll() {
    RefreshWeakTable();
    RefreshAccuracyTable();
    RefreshInspectionTable();
    RefreshActiveAssembly();
}

void UpdateControls() {
    const bool running = OperationRunner::Instance().IsRunning();
    const bool weakSelection = UiUtils::GetCheck(kDialog, kWeakUseSelection, true);
    const bool accuracySelection = UiUtils::GetCheck(kDialog, kAccUseSelection, true);
    const bool autoArrange = UiUtils::GetCheck(kDialog, kInspArrange, true);

    UiUtils::EnableButton(kDialog, kFooterCancel, running && !OperationRunner::Instance().IsCancelling());
    UiUtils::EnableButton(kDialog, "OverviewRunAllButton", !running);
    for (const char* button : {"WeakRunButton", "AccRunButton", "InspBuildButton"})
        UiUtils::EnableButton(kDialog, button, !running);
    for (const char* button : {"WeakBrowseButton", "AccBrowseButton", "InspBrowseButton"})
        UiUtils::EnableButton(kDialog, button, !running);
    for (const char* button : {"WeakClearButton", "AccClearButton", "InspClearButton"})
        UiUtils::EnableButton(kDialog, button, !running);

    UiUtils::EnableCheck(kDialog, kWeakUseSelection, !running);
    UiUtils::EnableInput(kDialog, kWeakFolder, !running && !weakSelection);
    UiUtils::EnableButton(kDialog, "WeakBrowseButton", !running && !weakSelection);
    UiUtils::EnableCheck(kDialog, kWeakRecursive, !running && !weakSelection);
    UiUtils::EnableCheck(kDialog, kWeakLatest, !running && !weakSelection);

    UiUtils::EnableCheck(kDialog, kAccUseSelection, !running);
    UiUtils::EnableInput(kDialog, kAccFolder, !running && !accuracySelection);
    UiUtils::EnableButton(kDialog, "AccBrowseButton", !running && !accuracySelection);
    UiUtils::EnableCheck(kDialog, kAccRecursive, !running && !accuracySelection);
    UiUtils::EnableCheck(kDialog, kAccLatest, !running && !accuracySelection);
    UiUtils::EnableCheck(kDialog, kAccParts, !running);
    UiUtils::EnableCheck(kDialog, kAccAssemblies, !running);

    for (const char* check : {kInspRecursive, kInspLatest, kInspParts, kInspAssemblies,
                              kInspFamily, kInspGeneric, kInspStep, kInspArrange})
        UiUtils::EnableCheck(kDialog, check, !running);
    UiUtils::EnableCheck(kDialog, kInspRowsAlongX, !running && autoArrange);
    UiUtils::EnableInput(kDialog, kInspFolder, !running);
    UiUtils::EnableInput(kDialog, kInspColumns, !running && autoArrange);
    UiUtils::EnableInput(kDialog, kInspGap, !running && autoArrange);
    if (!running) {
        RefreshWeakDetails();
        RefreshAccuracyDetails();
    }
}

void FinishOperation(const std::wstring& message) {
    g_operationName.clear();
    SetGlobalStatus(message);
    UpdateControls();
}

void OnOverviewRunAll(char*, char*, ProAppData) {
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
        const std::wstring message = selectError == PRO_TK_NO_ERROR ? L"No parts or assemblies selected."
            : L"Selection failed: " + ModelUtils::ErrorName(selectError);
        UiUtils::SetLabel(kDialog, kOverviewRunAllStatus, message);
        SetGlobalStatus(message);
        return;
    }
    auto& context = AppContext::Instance();
    context.runAllPending = std::move(models);
    g_operationName = L"Run all QC";
    const ProError startError = OperationRunner::Instance().Start(kDialog, context.runAllPending.size(),
        [](std::size_t index) {
            auto& state = AppContext::Instance();
            const auto& source = state.runAllPending[index];
            const std::wstring message = L"Run all QC | Checking " + std::to_wstring(index + 1) + L" / " +
                std::to_wstring(state.runAllPending.size()) + L": " + source.displayName;
            UiUtils::SetLabel(kDialog, kOverviewRunAllStatus, message);
            SetGlobalStatus(message);
            AccuracyResult accuracy = AccuracyChecker().Run(source.model);
            state.accuracyResults.push_back(std::move(accuracy));
            if (source.modelType == PRO_MDL_PART) {
                WeakResult weak = WeakDimensionChecker().Run(source.model);
                state.weakResults.push_back(std::move(weak));
            }
            RefreshWeakTable();
            RefreshAccuracyTable();
        },
        [](std::size_t done, std::size_t total) {
            SetGlobalStatus(L"Run all QC | " + std::to_wstring(done) + L" / " + std::to_wstring(total));
        },
        [](bool cancelled) {
            const std::wstring message = cancelled
                ? L"Run all QC cancelled. Partial results were kept."
                : L"Run all QC completed. Review the two QC tabs.";
            UiUtils::SetLabel(kDialog, kOverviewRunAllStatus, message);
            AppContext::Instance().runAllPending.clear();
            RefreshWeakTable();
            RefreshAccuracyTable();
            FinishOperation(message);
        });
    if (startError != PRO_TK_NO_ERROR) {
        context.runAllPending.clear();
        const std::wstring message = L"Could not start Run all QC: " + ModelUtils::ErrorName(startError);
        UiUtils::SetLabel(kDialog, kOverviewRunAllStatus, message);
        FinishOperation(message);
        return;
    }
    context.weakResults.clear();
    context.accuracyResults.clear();
    RefreshWeakTable();
    RefreshAccuracyTable();
    UpdateControls();
}

bool BrowseInto(const char* input, std::wstring& state, const std::wstring& title) {
    if (OperationRunner::Instance().IsRunning()) return false;
    std::wstring picked;
    const std::wstring current = UiUtils::GetInput(kDialog, input);
    if (!FolderPicker::Pick(title, current.empty() ? state : current, picked)) return false;
    state = picked;
    UiUtils::SetInput(kDialog, input, picked);
    return true;
}

void DiscoverWeak() {
    const auto scan = FolderScanner::Discover(UiUtils::GetInput(kDialog, kWeakFolder), WeakFolderOptions());
    UiUtils::SetLabel(kDialog, kWeakFound, scan.error.empty()
        ? L"Found " + std::to_wstring(scan.models.size()) + L" eligible parts."
        : scan.error);
}

void DiscoverAccuracy() {
    const auto scan = FolderScanner::Discover(UiUtils::GetInput(kDialog, kAccFolder), AccuracyFolderOptions());
    if (!scan.error.empty()) {
        UiUtils::SetLabel(kDialog, kAccFound, scan.error);
        return;
    }
    int parts = 0, assemblies = 0;
    for (const auto& source : scan.models) {
        if (source.modelType == PRO_MDL_PART) ++parts;
        else if (source.modelType == PRO_MDL_ASSEMBLY) ++assemblies;
    }
    UiUtils::SetLabel(kDialog, kAccFound, L"Found " + std::to_wstring(parts) + L" parts and " +
        std::to_wstring(assemblies) + L" assemblies.");
}

void DiscoverInspection() {
    InspectionOptions options;
    std::wstring error;
    if (!ReadInspectionOptions(options, error)) {
        UiUtils::SetLabel(kDialog, kInspValidation, error);
        return;
    }
    UiUtils::SetLabel(kDialog, kInspValidation, L"");
    const auto scan = FolderScanner::Discover(UiUtils::GetInput(kDialog, kInspFolder), InspectionFolderOptions(options));
    UiUtils::SetLabel(kDialog, kInspFound, scan.error.empty()
        ? L"Found " + std::to_wstring(scan.models.size()) + L" source files. Family and STEP processing may change the final component count."
        : scan.error);
}

void OnWeakBrowse(char*, char*, ProAppData) {
    auto& context = AppContext::Instance();
    if (BrowseInto(kWeakFolder, context.weakFolder, L"Select folder for Weak Dimension check")) DiscoverWeak();
}
void OnAccBrowse(char*, char*, ProAppData) {
    auto& context = AppContext::Instance();
    if (BrowseInto(kAccFolder, context.accuracyFolder, L"Select folder for Accuracy check")) DiscoverAccuracy();
}
void OnInspBrowse(char*, char*, ProAppData) {
    auto& context = AppContext::Instance();
    if (BrowseInto(kInspFolder, context.inspectionFolder, L"Select folder for Inspection Assembly Builder")) DiscoverInspection();
}

void StartWeakPending(bool cleanup) {
    auto& context = AppContext::Instance();
    if (OperationRunner::Instance().IsRunning() || context.weakPending.empty()) return;
    g_weakCleanup = cleanup;
    g_operationName = L"Weak dimensions";
    const ProError startError = OperationRunner::Instance().Start(kDialog, context.weakPending.size(),
        [](std::size_t index) {
            auto& state = AppContext::Instance();
            ModelDescriptor source = state.weakPending[index];
            const std::wstring message = L"Weak dimensions | Checking: " + source.displayName;
            UiUtils::SetLabel(kDialog, kWeakProgressText, message);
            SetGlobalStatus(message);
            SessionSnapshot before = SessionSnapshot::Capture();
            const ProError loadError = ModelLoader::Load(source, before);
            if (loadError != PRO_TK_NO_ERROR || !source.model) {
                WeakResult result;
                result.modelName = source.displayName;
                result.sourcePath = source.sourcePath;
                result.status = QcStatus::Error;
                result.errors = 1;
                result.summary = L"Load failed: " + ModelUtils::ErrorName(loadError);
                state.weakResults.push_back(std::move(result));
            } else {
                WeakResult result = WeakDimensionChecker().Run(source.model);
                result.sourcePath = source.sourcePath;
                if (g_weakCleanup) result.model = nullptr;
                state.weakResults.push_back(std::move(result));
            }
            if (g_weakCleanup) before.CleanupNewModels();
            RefreshWeakTable();
        },
        [](std::size_t done, std::size_t total) {
            UiUtils::SetProgress(kDialog, kWeakProgress, static_cast<int>(done), static_cast<int>(total));
            const std::wstring message = L"Weak dimensions | " + std::to_wstring(done) + L" / " + std::to_wstring(total);
            UiUtils::SetLabel(kDialog, kWeakProgressText, message);
            SetGlobalStatus(message);
        },
        [](bool cancelled) {
            const std::wstring message = cancelled ? L"Weak-dimension check cancelled. Partial results were kept."
                                                    : L"Weak-dimension check completed.";
            UiUtils::SetLabel(kDialog, kWeakProgressText, message);
            AppContext::Instance().weakPending.clear();
            RefreshWeakTable();
            FinishOperation(message);
        });
    if (startError != PRO_TK_NO_ERROR) {
        context.weakPending.clear();
        FinishOperation(L"Could not start Weak Dimensions: " + ModelUtils::ErrorName(startError));
        return;
    }
    context.weakResults.clear();
    RefreshWeakTable();
    UpdateControls();
}

void OnWeakRun(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    auto& context = AppContext::Instance();
    if (UiUtils::GetCheck(kDialog, kWeakUseSelection, true)) {
        std::vector<ModelDescriptor> models;
        ProUIDialogHide(const_cast<char*>(kDialog));
        const ProError error = SelectionService::SelectModels(true, false, models);
        ProUIDialogShow(const_cast<char*>(kDialog));
        if (error == PRO_TK_USER_ABORT) { SetGlobalStatus(L"Selection cancelled. Previous results were kept."); return; }
        if (error != PRO_TK_NO_ERROR || models.empty()) {
            SetGlobalStatus(error == PRO_TK_NO_ERROR ? L"No parts selected." : L"Selection failed: " + ModelUtils::ErrorName(error));
            return;
        }
        context.weakPending = std::move(models);
        StartWeakPending(false);
        return;
    }
    context.weakFolder = UiUtils::GetInput(kDialog, kWeakFolder);
    const auto scan = FolderScanner::Discover(context.weakFolder, WeakFolderOptions());
    if (!scan.error.empty() || scan.models.empty()) {
        const std::wstring message = scan.error.empty() ? L"No eligible parts found in this folder." : scan.error;
        UiUtils::SetLabel(kDialog, kWeakFound, message);
        SetGlobalStatus(message);
        return;
    }
    context.weakPending = scan.models;
    UiUtils::SetLabel(kDialog, kWeakFound, L"Found " + std::to_wstring(scan.models.size()) + L" eligible parts.");
    StartWeakPending(true);
}

void StartAccuracyPending(bool cleanup) {
    auto& context = AppContext::Instance();
    if (OperationRunner::Instance().IsRunning() || context.accuracyPending.empty()) return;
    g_accuracyCleanup = cleanup;
    g_operationName = L"Accuracy";
    const ProError startError = OperationRunner::Instance().Start(kDialog, context.accuracyPending.size(),
        [](std::size_t index) {
            auto& state = AppContext::Instance();
            ModelDescriptor source = state.accuracyPending[index];
            const std::wstring message = L"Accuracy | Checking: " + source.displayName;
            UiUtils::SetLabel(kDialog, kAccProgressText, message);
            SetGlobalStatus(message);
            SessionSnapshot before = SessionSnapshot::Capture();
            const ProError loadError = ModelLoader::Load(source, before);
            if (loadError != PRO_TK_NO_ERROR || !source.model) {
                AccuracyResult result;
                result.modelName = source.displayName;
                result.sourcePath = source.sourcePath;
                result.modelType = source.modelType;
                result.status = QcStatus::Error;
                result.details = L"Load failed: " + ModelUtils::ErrorName(loadError);
                state.accuracyResults.push_back(std::move(result));
            } else {
                AccuracyResult result = AccuracyChecker().Run(source.model);
                result.sourcePath = source.sourcePath;
                if (g_accuracyCleanup) result.model = nullptr;
                state.accuracyResults.push_back(std::move(result));
            }
            if (g_accuracyCleanup) before.CleanupNewModels();
            RefreshAccuracyTable();
        },
        [](std::size_t done, std::size_t total) {
            UiUtils::SetProgress(kDialog, kAccProgress, static_cast<int>(done), static_cast<int>(total));
            const std::wstring message = L"Accuracy | " + std::to_wstring(done) + L" / " + std::to_wstring(total);
            UiUtils::SetLabel(kDialog, kAccProgressText, message);
            SetGlobalStatus(message);
        },
        [](bool cancelled) {
            const std::wstring message = cancelled ? L"Accuracy check cancelled. Partial results were kept."
                                                    : L"Accuracy check completed.";
            UiUtils::SetLabel(kDialog, kAccProgressText, message);
            AppContext::Instance().accuracyPending.clear();
            RefreshAccuracyTable();
            FinishOperation(message);
        });
    if (startError != PRO_TK_NO_ERROR) {
        context.accuracyPending.clear();
        FinishOperation(L"Could not start Accuracy: " + ModelUtils::ErrorName(startError));
        return;
    }
    context.accuracyResults.clear();
    RefreshAccuracyTable();
    UpdateControls();
}

void OnAccuracyRun(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    const bool parts = UiUtils::GetCheck(kDialog, kAccParts, true);
    const bool assemblies = UiUtils::GetCheck(kDialog, kAccAssemblies, true);
    if (!parts && !assemblies) {
        SetGlobalStatus(L"Select Parts, Assemblies, or both before checking accuracy.");
        return;
    }
    auto& context = AppContext::Instance();
    if (UiUtils::GetCheck(kDialog, kAccUseSelection, true)) {
        std::vector<ModelDescriptor> models;
        ProUIDialogHide(const_cast<char*>(kDialog));
        const ProError error = SelectionService::SelectModels(parts, assemblies, models);
        ProUIDialogShow(const_cast<char*>(kDialog));
        if (error == PRO_TK_USER_ABORT) { SetGlobalStatus(L"Selection cancelled. Previous results were kept."); return; }
        if (error != PRO_TK_NO_ERROR || models.empty()) {
            SetGlobalStatus(error == PRO_TK_NO_ERROR ? L"No matching models selected." : L"Selection failed: " + ModelUtils::ErrorName(error));
            return;
        }
        context.accuracyPending = std::move(models);
        StartAccuracyPending(false);
        return;
    }
    context.accuracyFolder = UiUtils::GetInput(kDialog, kAccFolder);
    const auto scan = FolderScanner::Discover(context.accuracyFolder, AccuracyFolderOptions());
    if (!scan.error.empty() || scan.models.empty()) {
        const std::wstring message = scan.error.empty() ? L"No eligible models found in this folder." : scan.error;
        UiUtils::SetLabel(kDialog, kAccFound, message);
        SetGlobalStatus(message);
        return;
    }
    context.accuracyPending = scan.models;
    UiUtils::SetLabel(kDialog, kAccFound, L"Found " + std::to_wstring(scan.models.size()) + L" eligible models.");
    StartAccuracyPending(true);
}

void OnInspectionBuild(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    ProMdl current = nullptr;
    if (ModelUtils::CurrentModel(&current) != PRO_TK_NO_ERROR || !ModelUtils::IsAssembly(current)) {
        RefreshActiveAssembly();
        SetGlobalStatus(L"Open or create an assembly before adding inspection components.");
        return;
    }
    InspectionOptions options;
    std::wstring validationError;
    if (!ReadInspectionOptions(options, validationError)) {
        UiUtils::SetLabel(kDialog, kInspValidation, validationError);
        SetGlobalStatus(validationError);
        return;
    }
    UiUtils::SetLabel(kDialog, kInspValidation, L"");
    auto& context = AppContext::Instance();
    context.inspectionFolder = UiUtils::GetInput(kDialog, kInspFolder);
    const auto scan = FolderScanner::Discover(context.inspectionFolder, InspectionFolderOptions(options));
    if (!scan.error.empty() || scan.models.empty()) {
        const std::wstring message = scan.error.empty() ? L"No eligible source files found in this folder." : scan.error;
        UiUtils::SetLabel(kDialog, kInspFound, message);
        SetGlobalStatus(message);
        return;
    }
    context.inspectionPending = scan.models;
    g_inspectionBuilder = std::make_unique<InspectionBuilder>(reinterpret_cast<ProAssembly>(current), options);
    g_operationName = L"Inspection";
    const ProError startError = OperationRunner::Instance().Start(kDialog, context.inspectionPending.size(),
        [](std::size_t index) {
            auto& state = AppContext::Instance();
            const auto& source = state.inspectionPending[index];
            const std::wstring message = L"Inspection | Adding: " + source.displayName;
            UiUtils::SetLabel(kDialog, kInspProgressText, message);
            SetGlobalStatus(message);
            if (g_inspectionBuilder) g_inspectionBuilder->Process(source, state.inspectionResults);
            RefreshInspectionTable();
        },
        [](std::size_t done, std::size_t total) {
            UiUtils::SetProgress(kDialog, kInspProgress, static_cast<int>(done), static_cast<int>(total));
            const std::wstring message = L"Inspection | " + std::to_wstring(done) + L" / " + std::to_wstring(total);
            UiUtils::SetLabel(kDialog, kInspProgressText, message);
            SetGlobalStatus(message);
        },
        [](bool cancelled) {
            const std::wstring message = cancelled
                ? L"Inspection cancelled. Added components remain; the assembly was not saved."
                : L"Inspection completed. The assembly was not saved automatically.";
            UiUtils::SetLabel(kDialog, kInspProgressText, message);
            AppContext::Instance().inspectionPending.clear();
            g_inspectionBuilder.reset();
            RefreshInspectionTable();
            RefreshActiveAssembly();
            FinishOperation(message);
        });
    if (startError != PRO_TK_NO_ERROR) {
        context.inspectionPending.clear();
        g_inspectionBuilder.reset();
        FinishOperation(L"Could not start Inspection: " + ModelUtils::ErrorName(startError));
        return;
    }
    context.inspectionResults.clear();
    UiUtils::SetLabel(kDialog, kInspFound, L"Found " + std::to_wstring(scan.models.size()) + L" source files.");
    RefreshInspectionTable();
    UpdateControls();
}

void OnCancel(char*, char*, ProAppData) {
    if (!OperationRunner::Instance().IsRunning() || OperationRunner::Instance().IsCancelling()) return;
    OperationRunner::Instance().RequestCancel();
    const std::wstring message = L"Cancelling " + (g_operationName.empty() ? L"current operation" : g_operationName) + L" after the current model...";
    SetGlobalStatus(message);
    UpdateControls();
}

void OnWeakClear(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    AppContext::Instance().weakResults.clear();
    RefreshWeakTable();
}
void OnAccClear(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    AppContext::Instance().accuracyResults.clear();
    RefreshAccuracyTable();
}
void OnInspClear(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    AppContext::Instance().inspectionResults.clear();
    RefreshInspectionTable();
}

ProMdl ModelForWeakRow(int visibleIndex) {
    if (visibleIndex < 0 || static_cast<std::size_t>(visibleIndex) >= g_weakVisible.size()) return nullptr;
    auto& result = AppContext::Instance().weakResults[g_weakVisible[static_cast<std::size_t>(visibleIndex)].result];
    if (result.model) return result.model;
    if (result.sourcePath.empty()) return nullptr;
    ProMdl model = nullptr;
    if (ModelLoader::LoadPath(result.sourcePath, PRO_MDL_PART, &model) == PRO_TK_NO_ERROR) {
        result.model = model;
        return model;
    }
    UiUtils::SetLabel(kDialog, kWeakDetails, L"Could not load: " + result.sourcePath);
    return nullptr;
}

void OnWeakOpen(char*, char*, ProAppData) {
    if (ProMdl model = ModelForWeakRow(UiUtils::SelectedRowIndex(kDialog, kWeakTable, 'w'))) ModelUtils::DisplayModel(model);
}
void OnWeakGoto(char*, char*, ProAppData) {
    const int selected = UiUtils::SelectedRowIndex(kDialog, kWeakTable, 'w');
    if (selected < 0 || static_cast<std::size_t>(selected) >= g_weakVisible.size()) return;
    const auto visible = g_weakVisible[static_cast<std::size_t>(selected)];
    if (visible.finding < 0) return;
    ProMdl model = ModelForWeakRow(selected);
    if (!model) return;
    const auto& finding = AppContext::Instance().weakResults[visible.result].findings[static_cast<std::size_t>(visible.finding)];
    if (finding.featureId >= 0 && ModelUtils::HighlightFeature(model, finding.featureId) != PRO_TK_NO_ERROR)
        UiUtils::SetLabel(kDialog, kWeakDetails, L"The feature could not be highlighted in Creo.");
}
void OnAccOpen(char*, char*, ProAppData) {
    const int selected = UiUtils::SelectedRowIndex(kDialog, kAccTable, 'a');
    if (selected < 0 || static_cast<std::size_t>(selected) >= g_accuracyVisible.size()) return;
    auto& result = AppContext::Instance().accuracyResults[g_accuracyVisible[static_cast<std::size_t>(selected)]];
    ProMdl model = result.model;
    if (!model && !result.sourcePath.empty()) ModelLoader::LoadPath(result.sourcePath, result.modelType, &model);
    if (model) { result.model = model; ModelUtils::DisplayModel(model); }
    else UiUtils::SetLabel(kDialog, kAccDetails, L"Could not load: " + result.sourcePath);
}

void OnWeakSourceChanged(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    auto& context = AppContext::Instance();
    context.weakUseSelection = UiUtils::GetCheck(kDialog, kWeakUseSelection, true);
    UiUtils::SetLabel(kDialog, kWeakFound, context.weakUseSelection
        ? L"Selection mode: choose parts after starting the check."
        : L"Folder mode: choose a folder and options, then start the check.");
    UpdateControls();
}
void OnAccSourceChanged(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    auto& context = AppContext::Instance();
    context.accuracyUseSelection = UiUtils::GetCheck(kDialog, kAccUseSelection, true);
    UiUtils::SetLabel(kDialog, kAccFound, context.accuracyUseSelection
        ? L"Selection mode: choose models after starting the check."
        : L"Folder mode: choose a folder and options, then start the check.");
    UpdateControls();
}
void OnWeakSearch(char*, char*, ProAppData) {
    AppContext::Instance().weakSearch = UiUtils::GetInput(kDialog, kWeakSearch);
    RefreshWeakTable();
}
void OnAccSearch(char*, char*, ProAppData) {
    AppContext::Instance().accuracySearch = UiUtils::GetInput(kDialog, kAccSearch);
    RefreshAccuracyTable();
}
void OnWeakFilter(char*, char*, ProAppData) {
    AppContext::Instance().weakIssuesOnly = UiUtils::GetCheck(kDialog, kWeakIssuesOnly, false);
    RefreshWeakTable();
}
void OnAccFilter(char*, char*, ProAppData) {
    AppContext::Instance().accuracyIssuesOnly = UiUtils::GetCheck(kDialog, kAccIssuesOnly, false);
    RefreshAccuracyTable();
}
void OnWeakSetupChanged(char*, char*, ProAppData) {
    if (!OperationRunner::Instance().IsRunning() && !UiUtils::GetCheck(kDialog, kWeakUseSelection, true))
        UiUtils::SetLabel(kDialog, kWeakFound, L"Folder settings changed. Run the check to refresh discovery.");
}
void OnAccSetupChanged(char*, char*, ProAppData) {
    if (!OperationRunner::Instance().IsRunning() && !UiUtils::GetCheck(kDialog, kAccUseSelection, true))
        UiUtils::SetLabel(kDialog, kAccFound, L"Folder settings changed. Run the check to refresh discovery.");
}
void OnInspSetupChanged(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) return;
    UiUtils::SetLabel(kDialog, kInspFound, L"Source settings changed. Run Add to assembly to refresh discovery.");
    UpdateControls();
    InspectionOptions options;
    std::wstring error;
    ReadInspectionOptions(options, error);
    UiUtils::SetLabel(kDialog, kInspValidation, error);
}
void OnWeakSelected(char*, char*, ProAppData) { RefreshWeakDetails(); }
void OnAccSelected(char*, char*, ProAppData) { RefreshAccuracyDetails(); }
void OnInspSelected(char*, char*, ProAppData) { RefreshInspectionDetails(); }

void SaveUiState() {
    auto& context = AppContext::Instance();
    context.weakFolder = UiUtils::GetInput(kDialog, kWeakFolder);
    context.accuracyFolder = UiUtils::GetInput(kDialog, kAccFolder);
    context.inspectionFolder = UiUtils::GetInput(kDialog, kInspFolder);
    context.weakSearch = UiUtils::GetInput(kDialog, kWeakSearch);
    context.accuracySearch = UiUtils::GetInput(kDialog, kAccSearch);
    context.weakUseSelection = UiUtils::GetCheck(kDialog, kWeakUseSelection, true);
    context.accuracyUseSelection = UiUtils::GetCheck(kDialog, kAccUseSelection, true);
    context.weakIssuesOnly = UiUtils::GetCheck(kDialog, kWeakIssuesOnly, false);
    context.accuracyIssuesOnly = UiUtils::GetCheck(kDialog, kAccIssuesOnly, false);
    context.weakRecursive = UiUtils::GetCheck(kDialog, kWeakRecursive, false);
    context.weakLatest = UiUtils::GetCheck(kDialog, kWeakLatest, true);
    context.accuracyRecursive = UiUtils::GetCheck(kDialog, kAccRecursive, false);
    context.accuracyLatest = UiUtils::GetCheck(kDialog, kAccLatest, true);
    context.accuracyParts = UiUtils::GetCheck(kDialog, kAccParts, true);
    context.accuracyAssemblies = UiUtils::GetCheck(kDialog, kAccAssemblies, true);
    context.inspectionRecursive = UiUtils::GetCheck(kDialog, kInspRecursive, false);
    context.inspectionLatest = UiUtils::GetCheck(kDialog, kInspLatest, true);
    context.inspectionParts = UiUtils::GetCheck(kDialog, kInspParts, true);
    context.inspectionAssemblies = UiUtils::GetCheck(kDialog, kInspAssemblies, true);
    context.inspectionFamilyInstances = UiUtils::GetCheck(kDialog, kInspFamily, true);
    context.inspectionIncludeGeneric = UiUtils::GetCheck(kDialog, kInspGeneric, false);
    context.inspectionStep = UiUtils::GetCheck(kDialog, kInspStep, true);
    context.inspectionAutoArrange = UiUtils::GetCheck(kDialog, kInspArrange, true);
    context.inspectionRowsAlongX = UiUtils::GetCheck(kDialog, kInspRowsAlongX, false);
    int columns = context.inspectionColumns;
    double gap = context.inspectionGap;
    if (ParsePositiveInt(UiUtils::GetInput(kDialog, kInspColumns), columns)) context.inspectionColumns = columns;
    if (ParseNonNegativeDouble(UiUtils::GetInput(kDialog, kInspGap), gap)) context.inspectionGap = gap;
}

void OnClose(char*, char*, ProAppData) {
    if (OperationRunner::Instance().IsRunning()) {
        OnCancel(nullptr, nullptr, nullptr);
        SetGlobalStatus(L"Cancelling the current operation. Close again after it finishes.");
        return;
    }
    SaveUiState();
    ProUIDialogExit(const_cast<char*>(kDialog), PRO_TK_NO_ERROR);
}

void RegisterButton(const char* name, ProUIAction action) {
    ProUIPushbuttonActivateActionSet(const_cast<char*>(kDialog), const_cast<char*>(name), action, nullptr);
}
void RegisterCheck(const char* name, ProUIAction action) {
    ProUICheckbuttonActivateActionSet(const_cast<char*>(kDialog), const_cast<char*>(name), action, nullptr);
}
void RegisterInput(const char* name, ProUIAction action) {
    ProUIInputpanelInputActionSet(const_cast<char*>(kDialog), const_cast<char*>(name), action, nullptr);
}

void SetupTabs() {
    wchar_t* labels[] = {const_cast<wchar_t*>(L"Overview"), const_cast<wchar_t*>(L"Weak Dimensions"),
                         const_cast<wchar_t*>(L"Accuracy"), const_cast<wchar_t*>(L"Inspection")};
    ProUITabLabelsSet(const_cast<char*>(kDialog), const_cast<char*>(kTabs), 4, labels);
    ProUITabDecorate(const_cast<char*>(kDialog), const_cast<char*>(kTabs));
    ProUITabShow(const_cast<char*>(kDialog), const_cast<char*>(kTabs));
}

void SetupCallbacks() {
    RegisterButton("OverviewWeakButton", OnOverviewWeak);
    RegisterButton("OverviewAccButton", OnOverviewAccuracy);
    RegisterButton("OverviewInspButton", OnOverviewInspection);
    RegisterButton("OverviewRunAllButton", OnOverviewRunAll);
    RegisterButton("WeakBrowseButton", OnWeakBrowse);
    RegisterButton("WeakRunButton", OnWeakRun);
    RegisterButton("WeakClearButton", OnWeakClear);
    RegisterButton("WeakOpenButton", OnWeakOpen);
    RegisterButton("WeakGotoButton", OnWeakGoto);
    RegisterButton("AccBrowseButton", OnAccBrowse);
    RegisterButton("AccRunButton", OnAccuracyRun);
    RegisterButton("AccClearButton", OnAccClear);
    RegisterButton("AccOpenButton", OnAccOpen);
    RegisterButton("InspBrowseButton", OnInspBrowse);
    RegisterButton("InspBuildButton", OnInspectionBuild);
    RegisterButton("InspClearButton", OnInspClear);
    RegisterButton(kFooterCancel, OnCancel);
    RegisterButton("CloseButton", OnClose);
    ProUIDialogCloseActionSet(const_cast<char*>(kDialog), OnClose, nullptr);

    RegisterCheck(kWeakUseSelection, OnWeakSourceChanged);
    RegisterCheck(kAccUseSelection, OnAccSourceChanged);
    RegisterCheck(kWeakIssuesOnly, OnWeakFilter);
    RegisterCheck(kAccIssuesOnly, OnAccFilter);
    for (const char* check : {kWeakRecursive, kWeakLatest}) RegisterCheck(check, OnWeakSetupChanged);
    for (const char* check : {kAccRecursive, kAccLatest, kAccParts, kAccAssemblies}) RegisterCheck(check, OnAccSetupChanged);
    for (const char* check : {kInspRecursive, kInspLatest, kInspParts, kInspAssemblies,
                              kInspFamily, kInspGeneric, kInspStep, kInspArrange, kInspRowsAlongX})
        RegisterCheck(check, OnInspSetupChanged);
    RegisterInput(kWeakFolder, OnWeakSetupChanged);
    RegisterInput(kAccFolder, OnAccSetupChanged);
    RegisterInput(kInspFolder, OnInspSetupChanged);
    RegisterInput(kInspColumns, OnInspSetupChanged);
    RegisterInput(kInspGap, OnInspSetupChanged);
    RegisterInput(kWeakSearch, OnWeakSearch);
    RegisterInput(kAccSearch, OnAccSearch);

    ProUITableSelectionpolicySet(const_cast<char*>(kDialog), const_cast<char*>(kWeakTable), PROUISELPOLICY_SINGLE);
    ProUITableSelectionpolicySet(const_cast<char*>(kDialog), const_cast<char*>(kAccTable), PROUISELPOLICY_SINGLE);
    ProUITableSelectionpolicySet(const_cast<char*>(kDialog), const_cast<char*>(kInspTable), PROUISELPOLICY_SINGLE);
    ProUITableSelectActionSet(const_cast<char*>(kDialog), const_cast<char*>(kWeakTable), OnWeakSelected, nullptr);
    ProUITableSelectActionSet(const_cast<char*>(kDialog), const_cast<char*>(kAccTable), OnAccSelected, nullptr);
    ProUITableSelectActionSet(const_cast<char*>(kDialog), const_cast<char*>(kInspTable), OnInspSelected, nullptr);
}

void RestoreState() {
    auto& context = AppContext::Instance();
    UiUtils::SetInput(kDialog, kWeakFolder, context.weakFolder);
    UiUtils::SetInput(kDialog, kAccFolder, context.accuracyFolder);
    UiUtils::SetInput(kDialog, kInspFolder, context.inspectionFolder);
    UiUtils::SetInput(kDialog, kWeakSearch, context.weakSearch);
    UiUtils::SetInput(kDialog, kAccSearch, context.accuracySearch);
    UiUtils::SetCheck(kDialog, kWeakUseSelection, context.weakUseSelection);
    UiUtils::SetCheck(kDialog, kAccUseSelection, context.accuracyUseSelection);
    UiUtils::SetCheck(kDialog, kWeakIssuesOnly, context.weakIssuesOnly);
    UiUtils::SetCheck(kDialog, kAccIssuesOnly, context.accuracyIssuesOnly);
    UiUtils::SetCheck(kDialog, kWeakRecursive, context.weakRecursive);
    UiUtils::SetCheck(kDialog, kWeakLatest, context.weakLatest);
    UiUtils::SetCheck(kDialog, kAccRecursive, context.accuracyRecursive);
    UiUtils::SetCheck(kDialog, kAccLatest, context.accuracyLatest);
    UiUtils::SetCheck(kDialog, kAccParts, context.accuracyParts);
    UiUtils::SetCheck(kDialog, kAccAssemblies, context.accuracyAssemblies);
    UiUtils::SetCheck(kDialog, kInspRecursive, context.inspectionRecursive);
    UiUtils::SetCheck(kDialog, kInspLatest, context.inspectionLatest);
    UiUtils::SetCheck(kDialog, kInspParts, context.inspectionParts);
    UiUtils::SetCheck(kDialog, kInspAssemblies, context.inspectionAssemblies);
    UiUtils::SetCheck(kDialog, kInspFamily, context.inspectionFamilyInstances);
    UiUtils::SetCheck(kDialog, kInspGeneric, context.inspectionIncludeGeneric);
    UiUtils::SetCheck(kDialog, kInspStep, context.inspectionStep);
    UiUtils::SetCheck(kDialog, kInspArrange, context.inspectionAutoArrange);
    UiUtils::SetCheck(kDialog, kInspRowsAlongX, context.inspectionRowsAlongX);
    UiUtils::SetInput(kDialog, kInspColumns, std::to_wstring(context.inspectionColumns));
    UiUtils::SetInput(kDialog, kInspGap, DoubleText(context.inspectionGap));
    UiUtils::SetProgress(kDialog, kWeakProgress, 0, 1);
    UiUtils::SetProgress(kDialog, kAccProgress, 0, 1);
    UiUtils::SetProgress(kDialog, kInspProgress, 0, 1);
    UiUtils::SetLabel(kDialog, kWeakFound, context.weakUseSelection
        ? L"Selection mode: choose parts after starting the check."
        : L"Folder mode: choose a folder and options, then start the check.");
    UiUtils::SetLabel(kDialog, kAccFound, context.accuracyUseSelection
        ? L"Selection mode: choose models after starting the check."
        : L"Folder mode: choose a folder and options, then start the check.");
}
}

ProError ToolboxDialog::Show() {
    const ProError createError = ProUIDialogCreate(const_cast<char*>(kDialog), const_cast<char*>(kDialog));
    if (createError != PRO_TK_NO_ERROR) {
        Logger::Error(L"ProUIDialogCreate failed: " + ModelUtils::ErrorName(createError));
        return createError;
    }
    SetupTabs();
    SetupCallbacks();
    RestoreState();
    RefreshAll();
    UpdateControls();
    int status = 0;
    const ProError activateError = ProUIDialogActivate(const_cast<char*>(kDialog), &status);
    ProUIDialogDestroy(const_cast<char*>(kDialog));
    return activateError;
}
