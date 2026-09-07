#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include <string>

struct StepImportResult {
    ProError error = PRO_TK_GENERAL_ERROR;
    ProMdl model = nullptr;
    ProMdlType modelType = PRO_MDL_UNUSED;
    std::wstring createdName;
};

class StepImporter {
public:
    static StepImportResult Import(const std::wstring& sourcePath);
};
