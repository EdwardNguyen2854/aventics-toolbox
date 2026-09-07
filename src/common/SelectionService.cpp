#include <ProToolkit.h>
#include "common/SelectionService.h"
#include "common/ModelUtils.h"
#include "common/Logger.h"

#include <ProAsmcomppath.h>
#include <ProModelitem.h>
#include <ProSelection.h>

#include <algorithm>

namespace {
ProMdl ModelFromSelection(ProSelection selection) {
    if (!selection) return nullptr;
    ProAsmcomppath path;
    if (ProSelectionAsmcomppathGet(selection, &path) == PRO_TK_NO_ERROR && path.table_num > 0) {
        ProMdl model = nullptr;
        if (ProAsmcomppathMdlGet(&path, &model) == PRO_TK_NO_ERROR) return model;
    }
    ProModelitem item;
    if (ProSelectionModelitemGet(selection, &item) == PRO_TK_NO_ERROR) return item.owner;
    return nullptr;
}

bool Contains(const std::vector<ModelDescriptor>& models, ProMdl candidate) {
    return std::any_of(models.begin(), models.end(), [&](const ModelDescriptor& d) {
        return ModelUtils::SameModel(d.model, candidate);
    });
}
}

ProError SelectionService::SelectModels(bool includeParts, bool includeAssemblies, std::vector<ModelDescriptor>& outModels) {
    ProSelection* selections = nullptr;
    int count = 0;
    const ProError err = ProSelect(const_cast<char*>("prt_or_asm"), 512,
                                   nullptr, nullptr, nullptr, nullptr,
                                   &selections, &count);
    if (err != PRO_TK_NO_ERROR) return err;

    for (int i = 0; i < count; ++i) {
        ProMdl mdl = ModelFromSelection(selections[i]);
        if (!mdl || Contains(outModels, mdl)) continue;

        ProMdlType type = PRO_MDL_UNUSED;
        if (ProMdlTypeGet(mdl, &type) != PRO_TK_NO_ERROR) continue;
        if ((type == PRO_MDL_PART && !includeParts) ||
            (type == PRO_MDL_ASSEMBLY && !includeAssemblies) ||
            (type != PRO_MDL_PART && type != PRO_MDL_ASSEMBLY)) continue;

        ModelDescriptor d;
        d.model = mdl;
        d.modelType = type;
        d.sourceType = type == PRO_MDL_PART ? ModelSourceType::CreoPart : ModelSourceType::CreoAssembly;
        d.displayName = ModelUtils::ModelName(mdl);
        d.modelName = d.displayName;
        const auto pos = d.modelName.rfind(L'.');
        if (pos != std::wstring::npos) d.modelName.resize(pos);
        d.alreadyInSession = true;
        d.loadedByToolbox = false;
        outModels.push_back(std::move(d));
    }

    Logger::Info(L"Creo selection resolved to " + std::to_wstring(outModels.size()) + L" unique model(s).");
    return PRO_TK_NO_ERROR;
}
