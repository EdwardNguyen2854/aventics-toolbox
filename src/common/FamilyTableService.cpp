#include <ProToolkit.h>
#include "common/FamilyTableService.h"
#include "common/ModelUtils.h"

#include <ProFamtable.h>
#include <ProFaminstance.h>

#include <utility>

namespace {
struct VisitContext { std::vector<FamilyInstanceInfo>* instances = nullptr; };

ProError VisitInstance(ProFaminstance* instance, ProError status, ProAppData appData) {
    (void)status;
    auto* context = static_cast<VisitContext*>(appData);
    if (!instance || !context || !context->instances) return PRO_TK_BAD_INPUTS;

    FamilyInstanceInfo info;
    info.instanceName = instance->name;
    info.error = ProFaminstanceRetrieve(instance, &info.model);
    if (info.error == PRO_TK_NO_ERROR && info.model)
        info.instanceName = ModelUtils::ModelName(info.model);
    context->instances->push_back(std::move(info));
    return PRO_TK_NO_ERROR;
}
}

ProError FamilyTableService::RetrieveInstances(ProMdl generic, std::vector<FamilyInstanceInfo>& instances) {
    instances.clear();
    if (!generic) return PRO_TK_BAD_INPUTS;
    ProFamtable famtable;
    ProError err = ProFamtableInit(generic, &famtable);
    if (err == PRO_TK_UNSUPPORTED) return PRO_TK_E_NOT_FOUND;
    if (err != PRO_TK_NO_ERROR) return err;

    VisitContext context{ &instances };
    err = ProFamtableInstanceVisit(&famtable, VisitInstance, nullptr, &context);
    if (err == PRO_TK_E_NOT_FOUND) return PRO_TK_NO_ERROR;
    return err;
}
