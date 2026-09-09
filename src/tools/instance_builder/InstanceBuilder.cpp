#include <ProToolkit.h>
#include "tools/InstanceBuilder.h"

#include "common/FamilyTableService.h"
#include "common/Logger.h"
#include "common/ModelLoader.h"
#include "common/ModelUtils.h"
#include "common/SessionSnapshot.h"

#include <ProAsmcomp.h>
#include <ProMdl.h>
#include <ProSolid.h>

#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace {
std::wstring Trim(std::wstring value) {
    const auto notSpace = [](wchar_t c) { return !std::iswspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

std::wstring Key(const std::wstring& value) {
    std::wstring key = Lower(Trim(value));
    for (const std::wstring suffix : {L".prt", L".asm"}) {
        if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
            key.erase(key.size() - suffix.size());
            break;
        }
    }
    return key;
}

void Identity(ProMatrix matrix) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            matrix[r][c] = r == c ? 1.0 : 0.0;
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::string Csv(const std::wstring& value) {
    std::string utf8 = Utf8(value);
    std::string escaped;
    escaped.reserve(utf8.size() + 2);
    escaped.push_back('"');
    for (char c : utf8) {
        if (c == '"') escaped += "\"\"";
        else escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

std::wstring ModelTypeText(ProMdlType type) {
    return ModelUtils::ModelTypeName(type);
}
}

InstanceBuilder::InstanceBuilder(ProAssembly targetAssembly,
                                 const InstanceBuilderOptions& options,
                                 std::vector<InstanceRequest> requests)
    : targetAssembly_(targetAssembly), options_(options), requests_(std::move(requests)), resolutions_(requests_.size()) {
    if (options_.columns < 1) options_.columns = 1;
    if (options_.gap < 0.0) options_.gap = 0.0;
    if (options_.columnGap < 0.0) options_.columnGap = options_.gap;
    if (options_.rowGap < 0.0) options_.rowGap = options_.gap;
}

bool InstanceBuilder::ParseRequests(const std::wstring& text,
                                    int columns,
                                    std::vector<InstanceRequest>& requests,
                                    std::wstring& error) {
    requests.clear();
    if (columns < 1) {
        error = L"Columns must be a whole number of 1 or greater.";
        return false;
    }

    std::wstring token;
    const auto flush = [&]() {
        std::wstring code = Trim(token);
        token.clear();
        if (code.empty()) return;
        const int index = static_cast<int>(requests.size());
        requests.push_back({code, index / columns + 1, index % columns + 1});
    };

    for (wchar_t c : text) {
        if (c == L'\r' || c == L'\n' || c == L',' || c == L';' || c == L'\t') flush();
        else token.push_back(c);
    }
    flush();

    if (requests.empty()) {
        error = L"Enter at least one instance/code number.";
        return false;
    }
    error.clear();
    return true;
}

std::wstring InstanceBuilder::StatusName(InstanceBuildStatus status) {
    switch (status) {
    case InstanceBuildStatus::Planned: return L"PLANNED";
    case InstanceBuildStatus::Added: return L"ADDED";
    case InstanceBuildStatus::NotFound: return L"NOT FOUND";
    case InstanceBuildStatus::Failed: return L"FAILED";
    case InstanceBuildStatus::Skipped: return L"SKIPPED";
    }
    return L"UNKNOWN";
}

std::vector<std::wstring> InstanceBuilder::UnresolvedCodes() const {
    std::set<std::wstring> seen;
    std::vector<std::wstring> codes;
    for (std::size_t i = 0; i < requests_.size(); ++i) {
        if (resolutions_[i].model) continue;
        const std::wstring key = Key(requests_[i].code);
        if (key.empty() || !seen.insert(key).second) continue;
        codes.push_back(requests_[i].code);
    }
    return codes;
}

void InstanceBuilder::ResolveMatchingRequests(const std::wstring& name,
                                              ProMdl model,
                                              ProMdlType modelType,
                                              const std::wstring& genericName,
                                              const std::wstring& sourcePath,
                                              const std::wstring& error) {
    const std::wstring match = Key(name);
    if (match.empty()) return;
    for (std::size_t i = 0; i < requests_.size(); ++i) {
        if (resolutions_[i].model || Key(requests_[i].code) != match) continue;
        if (model) {
            resolutions_[i].model = model;
            resolutions_[i].modelType = modelType;
            resolutions_[i].genericName = genericName;
            resolutions_[i].sourcePath = sourcePath;
            resolutions_[i].error.clear();
        } else if (!error.empty()) {
            resolutions_[i].genericName = genericName;
            resolutions_[i].sourcePath = sourcePath;
            resolutions_[i].error = error;
        }
    }
}

void InstanceBuilder::ProcessSource(ModelDescriptor descriptor) {
    if (!targetAssembly_ || UnresolvedCodes().empty()) return;

    const SessionSnapshot before = SessionSnapshot::Capture();
    const ProError loadErr = ModelLoader::Load(descriptor, before);
    if (loadErr != PRO_TK_NO_ERROR || !descriptor.model) {
        Logger::Warn(L"Instance Builder could not load " + descriptor.displayName + L": " + ModelUtils::ErrorName(loadErr));
        return;
    }

    ProMdlType sourceType = PRO_MDL_UNUSED;
    ProMdlTypeGet(descriptor.model, &sourceType);
    const std::wstring genericName = ModelUtils::ModelName(descriptor.model);

    // An exact standalone model match is accepted as well. This also covers
    // family instances that already exist as independently retrievable models.
    ResolveMatchingRequests(genericName, descriptor.model, sourceType, L"", descriptor.sourcePath);

    const auto unresolved = UnresolvedCodes();
    if (unresolved.empty()) return;

    std::vector<FamilyInstanceInfo> matches;
    const ProError familyErr = FamilyTableService::RetrieveMatchingInstances(descriptor.model, unresolved, matches);
    if (familyErr != PRO_TK_NO_ERROR && familyErr != PRO_TK_E_NOT_FOUND) {
        Logger::Warn(L"Instance Builder family-table scan warning for " + genericName + L": " + ModelUtils::ErrorName(familyErr));
        return;
    }

    for (const auto& match : matches) {
        ProMdlType instanceType = PRO_MDL_UNUSED;
        if (match.model) ProMdlTypeGet(match.model, &instanceType);
        ResolveMatchingRequests(match.instanceName, match.model, instanceType, genericName, descriptor.sourcePath,
            match.error == PRO_TK_NO_ERROR ? L"" : L"Family instance retrieve failed: " + ModelUtils::ErrorName(match.error));
    }
}

std::size_t InstanceBuilder::ResolvedCount() const {
    return static_cast<std::size_t>(std::count_if(resolutions_.begin(), resolutions_.end(),
        [](const Resolution& resolution) { return resolution.model != nullptr; }));
}

void InstanceBuilder::Finalize(std::vector<InstanceBuildResult>& results) {
    results.clear();
    results.reserve(requests_.size());

    double maxXSpan = 1.0;
    double maxSecondarySpan = 1.0;
    for (const auto& resolution : resolutions_) {
        if (!resolution.model || !ModelUtils::IsSolid(resolution.model)) continue;
        Pro3dPnt outline[2]{};
        if (ProSolidOutlineGet(reinterpret_cast<ProSolid>(resolution.model), outline) != PRO_TK_NO_ERROR) continue;
        maxXSpan = std::max(maxXSpan, std::abs(outline[1][0] - outline[0][0]));
        const int secondaryAxis = options_.useZAxisForRows ? 2 : 1;
        maxSecondarySpan = std::max(maxSecondarySpan,
            std::abs(outline[1][secondaryAxis] - outline[0][secondaryAxis]));
    }

    const double xGap = options_.arrangeRowsAlongX ? options_.rowGap : options_.columnGap;
    const double secondaryGap = options_.arrangeRowsAlongX ? options_.columnGap : options_.rowGap;
    const double xStep = maxXSpan + xGap;
    const double secondaryStep = maxSecondarySpan + secondaryGap;

    for (std::size_t i = 0; i < requests_.size(); ++i) {
        const auto& request = requests_[i];
        const auto& resolution = resolutions_[i];
        InstanceBuildResult result;
        result.requestedCode = request.code;
        result.row = request.row;
        result.column = request.column;
        result.genericName = resolution.genericName;
        result.sourcePath = resolution.sourcePath;
        result.modelType = resolution.modelType;

        if (!resolution.model) {
            result.status = resolution.error.empty() ? InstanceBuildStatus::NotFound : InstanceBuildStatus::Failed;
            result.details = resolution.error.empty()
                ? L"Exact instance was not found in the scanned Creo parts and assemblies."
                : resolution.error;
            results.push_back(std::move(result));
            continue;
        }

        result.addedModelName = ModelUtils::ModelName(resolution.model);
        if (reinterpret_cast<ProMdl>(targetAssembly_) == resolution.model) {
            result.status = InstanceBuildStatus::Skipped;
            result.details = L"Skipped target assembly to prevent self-reference.";
            results.push_back(std::move(result));
            continue;
        }
        if (!ModelUtils::IsSolid(resolution.model)) {
            result.status = InstanceBuildStatus::Skipped;
            result.details = L"Resolved model is not a part or assembly.";
            results.push_back(std::move(result));
            continue;
        }

        Pro3dPnt outline[2]{};
        const ProError outlineErr = ProSolidOutlineGet(reinterpret_cast<ProSolid>(resolution.model), outline);
        if (outlineErr != PRO_TK_NO_ERROR) {
            result.status = InstanceBuildStatus::Failed;
            result.details = L"Could not calculate model outline for grid placement: " + ModelUtils::ErrorName(outlineErr);
            results.push_back(std::move(result));
            continue;
        }

        const double rowOffset = static_cast<double>(request.row - 1);
        const double columnOffset = static_cast<double>(request.column - 1);
        const double xPosition = (options_.arrangeRowsAlongX ? rowOffset : columnOffset) * xStep;
        const double secondaryPosition = (options_.arrangeRowsAlongX ? columnOffset : rowOffset) * secondaryStep;

        ProMatrix matrix{};
        Identity(matrix);
        matrix[3][0] = xPosition - outline[0][0];
        if (options_.useZAxisForRows) {
            matrix[3][1] = -outline[0][1];
            matrix[3][2] = secondaryPosition - outline[0][2];
        } else {
            matrix[3][1] = secondaryPosition - outline[0][1];
            matrix[3][2] = -outline[0][2];
        }

        ProAsmcomp component;
        const ProError assembleErr = ProAsmcompAssemble(targetAssembly_, reinterpret_cast<ProSolid>(resolution.model), matrix, &component);
        if (assembleErr == PRO_TK_NO_ERROR) {
            result.status = InstanceBuildStatus::Added;
            result.componentFeatureId = component.id;
            result.details = options_.useZAxisForRows
                ? L"Added unconstrained at allocated row/column on the X-Z plane."
                : L"Added unconstrained at allocated row/column on the X-Y plane.";
            Logger::Info(L"Instance Builder added " + result.addedModelName + L" at row " +
                         std::to_wstring(result.row) + L", column " + std::to_wstring(result.column));
        } else {
            result.status = InstanceBuildStatus::Failed;
            result.details = L"Assemble failed: " + ModelUtils::ErrorName(assembleErr);
            Logger::Error(L"Instance Builder assemble failed for " + result.addedModelName + L": " + ModelUtils::ErrorName(assembleErr));
        }
        results.push_back(std::move(result));
    }
}

bool InstanceBuilder::ExportCsv(const std::vector<InstanceBuildResult>& results,
                                std::wstring& message) {
    if (results.empty()) {
        message = L"There are no Instance Builder results to export.";
        return false;
    }

    wchar_t fileName[MAX_PATH] = L"instance_builder_report.csv";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) {
        message = L"Report export cancelled.";
        return false;
    }

    std::filesystem::path path(fileName);
    if (Lower(path.extension().wstring()) != L".csv") path += L".csv";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        message = L"Could not create report: " + path.wstring();
        return false;
    }

    // UTF-8 BOM helps Excel open product-code text reliably on Windows.
    stream.write("\xEF\xBB\xBF", 3);
    stream << "Requested Code,Row,Column,Generic,Source,Added Model,Model Type,Status,Feature ID,Details\r\n";
    for (const auto& result : results) {
        stream << Csv(result.requestedCode) << ','
               << result.row << ','
               << result.column << ','
               << Csv(result.genericName) << ','
               << Csv(result.sourcePath) << ','
               << Csv(result.addedModelName) << ','
               << Csv(ModelTypeText(result.modelType)) << ','
               << Csv(StatusName(result.status)) << ',';
        if (result.componentFeatureId >= 0) stream << result.componentFeatureId;
        stream << ',' << Csv(result.details) << "\r\n";
    }
    stream.close();
    if (!stream) {
        message = L"Report write failed: " + path.wstring();
        return false;
    }

    message = L"Report exported: " + path.wstring();
    return true;
}
