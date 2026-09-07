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
};
