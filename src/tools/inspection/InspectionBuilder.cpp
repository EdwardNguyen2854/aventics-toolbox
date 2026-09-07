#include <ProToolkit.h>
#include "tools/InspectionBuilder.h"
#include "common/FamilyTableService.h"
#include "common/Logger.h"
#include "common/ModelLoader.h"
#include "common/ModelUtils.h"
#include "common/SessionSnapshot.h"
#include "common/StepImporter.h"

#include <ProAsmcomp.h>
#include <ProMdl.h>
#include <utility>

InspectionBuilder::InspectionBuilder(ProAssembly targetAssembly, const InspectionOptions& options)
    : targetAssembly_(targetAssembly), options_(options), placement_(options) {}

void InspectionBuilder::Process(const ModelDescriptor& descriptor, std::vector<InspectionResult>& results) {
    if (!targetAssembly_) {
        results.push_back({descriptor.displayName, descriptor.sourcePath, L"", ModelUtils::SourceTypeName(descriptor.sourceType),
                           InspectionStatus::Failed, -1, L"No active target assembly."});
        return;
    }
    if (descriptor.sourceType == ModelSourceType::Step) ProcessStep(descriptor, results);
    else ProcessNative(descriptor, results);
}

void InspectionBuilder::ProcessNative(ModelDescriptor descriptor, std::vector<InspectionResult>& results) {
    const SessionSnapshot before = SessionSnapshot::Capture();
    const ProError loadErr = ModelLoader::Load(descriptor, before);
    if (loadErr != PRO_TK_NO_ERROR || !descriptor.model) {
        results.push_back({descriptor.displayName, descriptor.sourcePath, L"", ModelUtils::SourceTypeName(descriptor.sourceType),
                           InspectionStatus::Failed, -1, L"Load failed: " + ModelUtils::ErrorName(loadErr)});
        return;
    }

    if (reinterpret_cast<ProMdl>(targetAssembly_) == descriptor.model) {
        results.push_back({descriptor.displayName, descriptor.sourcePath, L"", ModelUtils::SourceTypeName(descriptor.sourceType),
                           InspectionStatus::Skipped, -1, L"Skipped target assembly to prevent self-reference."});
        return;
    }

    if (options_.includeFamilyInstances) {
        std::vector<FamilyInstanceInfo> instances;
        const ProError famErr = FamilyTableService::RetrieveInstances(descriptor.model, instances);
        if (famErr == PRO_TK_NO_ERROR && !instances.empty()) {
            if (options_.includeGenericWhenFamilyExists)
                AssembleOne(descriptor.model, descriptor.displayName, descriptor.sourcePath,
                            ModelUtils::SourceTypeName(descriptor.sourceType), results);

            for (const auto& instance : instances) {
                if (instance.error != PRO_TK_NO_ERROR || !instance.model) {
                    results.push_back({descriptor.displayName, descriptor.sourcePath, instance.instanceName, L"Family Instance",
                                       InspectionStatus::Warning, -1,
                                       L"Could not retrieve family instance: " + ModelUtils::ErrorName(instance.error)});
                    continue;
                }
                AssembleOne(instance.model, descriptor.displayName, descriptor.sourcePath, L"Family Instance", results);
            }
            return;
        }
        if (famErr != PRO_TK_NO_ERROR && famErr != PRO_TK_E_NOT_FOUND) {
            results.push_back({descriptor.displayName, descriptor.sourcePath, L"", ModelUtils::SourceTypeName(descriptor.sourceType),
                               InspectionStatus::Warning, -1, L"Family table query warning: " + ModelUtils::ErrorName(famErr)});
        }
    }

    AssembleOne(descriptor.model, descriptor.displayName, descriptor.sourcePath,
                ModelUtils::SourceTypeName(descriptor.sourceType), results);
}

void InspectionBuilder::ProcessStep(const ModelDescriptor& descriptor, std::vector<InspectionResult>& results) {
    const StepImportResult imported = StepImporter::Import(descriptor.sourcePath);
    if (imported.error != PRO_TK_NO_ERROR || !imported.model) {
        results.push_back({descriptor.displayName, descriptor.sourcePath, L"", L"STEP",
                           InspectionStatus::Failed, -1, L"STEP import failed: " + ModelUtils::ErrorName(imported.error)});
        return;
    }
    AssembleOne(imported.model, descriptor.displayName, descriptor.sourcePath, L"STEP", results);
}

void InspectionBuilder::AssembleOne(ProMdl model, const std::wstring& sourceName, const std::wstring& sourcePath,
                                    const std::wstring& kind, std::vector<InspectionResult>& results) {
    if (!model || !ModelUtils::IsSolid(model)) {
        results.push_back({sourceName, sourcePath, L"", kind, InspectionStatus::Skipped, -1, L"Model is not a part or assembly."});
        return;
    }
    if (reinterpret_cast<ProMdl>(targetAssembly_) == model) {
        results.push_back({sourceName, sourcePath, ModelUtils::ModelName(model), kind, InspectionStatus::Skipped, -1,
                           L"Skipped target assembly to prevent self-reference."});
        return;
    }

    const std::wstring key = ModelUtils::ToLower(ModelUtils::ModelName(model)) + L"|" + kind;
    if (!addedKeys_.insert(key).second) {
        results.push_back({sourceName, sourcePath, ModelUtils::ModelName(model), kind, InspectionStatus::Skipped, -1,
                           L"Duplicate top-level model skipped."});
        return;
    }

    ProMatrix matrix{};
    const ProError placementErr = placement_.NextTransform(model, matrix);
    if (placementErr != PRO_TK_NO_ERROR) {
        // Fallback to identity at origin if outline lookup fails.
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                matrix[r][c] = r == c ? 1.0 : 0.0;
    }

    ProAsmcomp component;
    const ProError err = ProAsmcompAssemble(targetAssembly_, reinterpret_cast<ProSolid>(model), matrix, &component);
    InspectionResult result;
    result.sourceName = sourceName;
    result.sourcePath = sourcePath;
    result.addedModelName = ModelUtils::ModelName(model);
    result.sourceKind = kind;
    result.componentFeatureId = err == PRO_TK_NO_ERROR ? component.id : -1;
    if (err == PRO_TK_NO_ERROR) {
        result.status = placementErr == PRO_TK_NO_ERROR ? InspectionStatus::Added : InspectionStatus::Warning;
        result.details = placementErr == PRO_TK_NO_ERROR ? L"Added unconstrained." : L"Added unconstrained at origin; auto-layout outline unavailable.";
        Logger::Info(L"Inspection added " + result.addedModelName);
    } else {
        result.status = InspectionStatus::Failed;
        result.details = L"Assemble failed: " + ModelUtils::ErrorName(err);
        Logger::Error(L"Inspection assemble failed for " + result.addedModelName + L": " + ModelUtils::ErrorName(err));
    }
    results.push_back(std::move(result));
}

