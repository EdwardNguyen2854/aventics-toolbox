#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include <string>
#include <vector>

struct FamilyInstanceInfo {
    std::wstring instanceName;
    ProMdl model = nullptr;
    ProError error = PRO_TK_NO_ERROR;
};

class FamilyTableService {
public:
    static ProError RetrieveInstances(ProMdl generic, std::vector<FamilyInstanceInfo>& instances);
    static ProError RetrieveMatchingInstances(ProMdl generic,
                                              const std::vector<std::wstring>& requestedNames,
                                              std::vector<FamilyInstanceInfo>& instances);
};
