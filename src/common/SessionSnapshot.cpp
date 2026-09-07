#include <ProToolkit.h>
#include "common/SessionSnapshot.h"
#include "common/Logger.h"
#include "common/ModelUtils.h"

#include <ProArray.h>
#include <ProMdl.h>

namespace {
void CollectType(ProMdlType type, std::set<ProMdl>& target) {
    ProMdl* models = nullptr;
    int count = 0;
    const ProError err = ProSessionMdlList(type, &models, &count);
    if (err == PRO_TK_NO_ERROR) {
        for (int i = 0; i < count; ++i) if (models[i]) target.insert(models[i]);
    }
    if (models) ProArrayFree(reinterpret_cast<ProArray*>(&models));
}
}

SessionSnapshot SessionSnapshot::Capture() {
    SessionSnapshot s;
    CollectType(PRO_MDL_PART, s.models_);
    CollectType(PRO_MDL_ASSEMBLY, s.models_);
    return s;
}

bool SessionSnapshot::Contains(ProMdl model) const {
    return model && models_.find(model) != models_.end();
}

void SessionSnapshot::CleanupNewModels(ProMdl preserve) const {
    std::set<ProMdl> after;
    CollectType(PRO_MDL_ASSEMBLY, after);
    CollectType(PRO_MDL_PART, after);

    ProMdl current = nullptr;
    ProMdlCurrentGet(&current);

    // Assemblies first, then parts, so dependencies are more likely to release.
    for (ProMdlType type : { PRO_MDL_ASSEMBLY, PRO_MDL_PART }) {
        for (ProMdl mdl : after) {
            if (!mdl || Contains(mdl) || mdl == preserve || mdl == current) continue;
            ProMdlType actual = PRO_MDL_UNUSED;
            if (ProMdlTypeGet(mdl, &actual) != PRO_TK_NO_ERROR || actual != type) continue;
            const ProError err = ProMdlErase(mdl);
            if (err != PRO_TK_NO_ERROR)
                Logger::Warn(L"Could not erase temporary model " + ModelUtils::ModelName(mdl) + L": " + ModelUtils::ErrorName(err));
        }
    }
}
