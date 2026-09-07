#include <ProToolkit.h>
#include "common/FolderScanner.h"
#include "common/ModelUtils.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>

namespace fs = std::filesystem;

namespace {
const std::wregex kCreoPattern(LR"(^(.+)\.(prt|asm)(?:\.(\d+))?$)", std::regex::icase);

bool ParseNative(const fs::path& path, ModelDescriptor& d) {
    const std::wstring file = path.filename().wstring();
    std::wsmatch m;
    if (!std::regex_match(file, m, kCreoPattern)) return false;

    const std::wstring base = m[1].str();
    const std::wstring kind = ModelUtils::ToLower(m[2].str());
    const int version = m[3].matched ? std::stoi(m[3].str()) : 0;

    d.sourceType = kind == L"prt" ? ModelSourceType::CreoPart : ModelSourceType::CreoAssembly;
    d.modelType = kind == L"prt" ? PRO_MDL_PART : PRO_MDL_ASSEMBLY;
    d.modelName = base;
    d.displayName = base + (kind == L"prt" ? L".prt" : L".asm");
    d.sourcePath = path.wstring();
    d.creoFileVersion = version;
    return true;
}

bool ParseStep(const fs::path& path, ModelDescriptor& d) {
    const std::wstring ext = ModelUtils::ToLower(path.extension().wstring());
    if (ext != L".stp" && ext != L".step") return false;
    d.sourceType = ModelSourceType::Step;
    d.modelType = PRO_MDL_UNUSED;
    d.modelName = path.stem().wstring();
    d.displayName = path.filename().wstring();
    d.sourcePath = path.wstring();
    d.creoFileVersion = -1;
    return true;
}

bool Allowed(const ModelDescriptor& d, const FolderScanOptions& o) {
    if (d.sourceType == ModelSourceType::CreoPart) return o.includeParts;
    if (d.sourceType == ModelSourceType::CreoAssembly) return o.includeAssemblies;
    if (d.sourceType == ModelSourceType::Step) return o.includeStep;
    return false;
}

void AddCandidate(const fs::path& p, const FolderScanOptions& o, std::vector<ModelDescriptor>& out) {
    ModelDescriptor d;
    if ((ParseNative(p, d) || ParseStep(p, d)) && Allowed(d, o)) out.push_back(std::move(d));
}
}

FolderScanResult FolderScanner::Discover(const std::wstring& folder, const FolderScanOptions& options) {
    FolderScanResult result;
    try {
        fs::path root(folder);
        if (folder.empty() || !fs::exists(root) || !fs::is_directory(root)) {
            result.error = L"Folder does not exist or is not a directory.";
            return result;
        }

        std::vector<ModelDescriptor> candidates;
        if (options.includeSubfolders) {
            for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied))
                if (entry.is_regular_file()) AddCandidate(entry.path(), options, candidates);
        } else {
            for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied))
                if (entry.is_regular_file()) AddCandidate(entry.path(), options, candidates);
        }

        if (!options.latestCreoVersionOnly) {
            result.models = std::move(candidates);
        } else {
            std::map<std::wstring, ModelDescriptor> latest;
            std::vector<ModelDescriptor> steps;
            for (auto& d : candidates) {
                if (d.sourceType == ModelSourceType::Step) {
                    steps.push_back(std::move(d));
                    continue;
                }
                // Version grouping is per directory + model name. Two different
                // subfolders may intentionally contain models with the same name.
                const std::filesystem::path src(d.sourcePath);
                const std::wstring key = ModelUtils::ToLower((src.parent_path() / d.modelName).wstring()) + L"|" +
                    (d.modelType == PRO_MDL_PART ? L"prt" : L"asm");
                auto it = latest.find(key);
                if (it == latest.end() || d.creoFileVersion > it->second.creoFileVersion)
                    latest[key] = std::move(d);
            }
            for (auto& kv : latest) result.models.push_back(std::move(kv.second));
            result.models.insert(result.models.end(), std::make_move_iterator(steps.begin()), std::make_move_iterator(steps.end()));
        }

        std::sort(result.models.begin(), result.models.end(), [](const ModelDescriptor& a, const ModelDescriptor& b) {
            return ModelUtils::ToLower(a.displayName) < ModelUtils::ToLower(b.displayName);
        });
    }
    catch (const std::exception&) {
        result.error = L"Folder enumeration failed. Check permissions and path length.";
    }
    return result;
}
