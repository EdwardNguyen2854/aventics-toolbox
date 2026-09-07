#include <ProToolkit.h>
#include "common/StepImporter.h"
#include "common/ModelUtils.h"

#include <ProIntfimport.h>
#include <atomic>
#include <iomanip>
#include <sstream>

namespace {
std::atomic<unsigned> g_stepCounter{1};

std::wstring NextName() {
    std::wstringstream name;
    name << L"AVT_STEP_" << std::setw(4) << std::setfill(L'0') << g_stepCounter.fetch_add(1);
    return name.str();
}
}

StepImportResult StepImporter::Import(const std::wstring& sourcePath) {
    StepImportResult result;

    ProPath importPath;
    if (!ModelUtils::CopyToProPath(sourcePath, importPath)) {
        result.error = PRO_TK_LINE_TOO_LONG;
        return result;
    }

    ProMdlType expected = PRO_MDL_UNUSED;
    const ProError typeErr = ProIntfimportSourceTypeGet(importPath, PRO_INTF_IMPORT_STEP, &expected);
    if (typeErr != PRO_TK_NO_ERROR && typeErr != PRO_TK_INVALID_TYPE) {
        result.error = typeErr;
        return result;
    }

    // Retry names when an earlier STEP import with the same generated name is
    // still in the Creo session. PTC returns PRO_TK_E_FOUND for this case.
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const std::wstring generated = NextName();
        ProMdlName newName;
        if (!ModelUtils::CopyToProMdlName(generated, newName)) {
            result.error = PRO_TK_LINE_TOO_LONG;
            return result;
        }

        result.model = nullptr;
        result.error = ProIntfimportModelWithOptionsMdlnameCreate(
            importPath,
            nullptr,
            PRO_INTF_IMPORT_STEP,
            expected == PRO_MDL_PART || expected == PRO_MDL_ASSEMBLY ? expected : PRO_MDL_UNUSED,
            PRO_IMPORTREP_MASTER,
            newName,
            nullptr,
            nullptr,
            &result.model);

        if (result.error == PRO_TK_E_FOUND) continue;
        if (result.error != PRO_TK_NO_ERROR || !result.model) return result;

        ProMdlType actual = PRO_MDL_UNUSED;
        if (ProMdlTypeGet(result.model, &actual) == PRO_TK_NO_ERROR)
            result.modelType = actual;
        result.createdName = ModelUtils::ModelName(result.model);
        return result;
    }

    result.error = PRO_TK_E_IN_USE;
    return result;
}
