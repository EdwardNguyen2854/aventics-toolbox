#include <ProToolkit.h>
#include "common/FamilyTableService.h"
#include "common/ModelUtils.h"

#include <ProFamtable.h>
#include <ProFaminstance.h>

#include <algorithm>
#include <cwctype>
#include <set>
#include <utility>

namespace {
std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

struct VisitContext {
    std::vector<FamilyInstanceInfo>* instances = nullptr;
    const std::set<std::wstring>* requested = nullptr;
};

ProError VisitInstance(ProFaminstance* instance, ProError status, ProAppData appData) {
    (void)status;
    auto* context = static_cast<VisitContext*>(appData);
    if (!instance || !context || !context->instances) return PRO_TK_BAD_INPUTS;

    const std::wstring rawName(instance->name);
    if (context->requested && context->requested->find(Lower(rawName)) == context->requested->end())
        return PRO_TK_NO_ERROR;

    FamilyInstanceInfo info;
    info.instanceName = rawName;
    info.error = ProFaminstanceRetrieve(instance, &info.model);
    if (info.error == PRO_TK_NO_ERROR && info.model)
        info.instanceName = ModelUtils::ModelName(info.model);
    context->instances->push_back(std::move(info));
    return PRO_TK_NO_ERROR;
}

ProError Visit(ProMdl generic, VisitContext& context) {
    if (!generic) return PRO_TK_BAD_INPUTS;
    ProFamtable famtable;
    ProError err = ProFamtableInit(generic, &famtable);
    if (err == PRO_TK_UNSUPPORTED) return PRO_TK_E_NOT_FOUND;
    if (err != PRO_TK_NO_ERROR) return err;

    err = ProFamtableInstanceVisit(&famtable, VisitInstance, nullptr, &context);
    if (err == PRO_TK_E_NOT_FOUND) return PRO_TK_NO_ERROR;
    return err;
}
}

ProError FamilyTableService::RetrieveInstances(ProMdl generic, std::vector<FamilyInstanceInfo>& instances) {
    instances.clear();
    VisitContext context{ &instances, nullptr };
    return Visit(generic, context);
}

ProError FamilyTableService::RetrieveMatchingInstances(ProMdl generic,
                                                       const std::vector<std::wstring>& requestedNames,
                                                       std::vector<FamilyInstanceInfo>& instances) {
    instances.clear();
    if (requestedNames.empty()) return PRO_TK_NO_ERROR;

    std::set<std::wstring> requested;
    for (const auto& name : requestedNames) {
        if (!name.empty()) requested.insert(Lower(name));
    }
    VisitContext context{ &instances, &requested };
    return Visit(generic, context);
}
