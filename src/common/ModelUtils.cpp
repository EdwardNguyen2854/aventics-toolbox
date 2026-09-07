#include <ProToolkit.h>
#include "common/ModelUtils.h"

#include <ProFeatType.h>
#include <ProModelitem.h>
#include <ProSelection.h>
#include <ProSolid.h>
#include <ProWindows.h>

#include <algorithm>
#include <cwchar>
#include <sstream>

namespace {
std::wstring FromWide(const wchar_t* text) {
    return text ? std::wstring(text) : std::wstring();
}
}

namespace ModelUtils {

std::wstring ModelName(ProMdl model) {
    if (!model) return L"<null>";

    ProMdlFileName displayName;
    displayName[0] = L'\0';
    if (ProMdlDisplaynameGet(model, PRO_B_TRUE, displayName) == PRO_TK_NO_ERROR)
        return FromWide(displayName);

    ProMdlName name;
    name[0] = L'\0';
    if (ProMdlMdlnameGet(model, name) == PRO_TK_NO_ERROR) {
        std::wstring value = FromWide(name);
        ProMdlType type = PRO_MDL_UNUSED;
        if (ProMdlTypeGet(model, &type) == PRO_TK_NO_ERROR) {
            if (type == PRO_MDL_PART) value += L".prt";
            else if (type == PRO_MDL_ASSEMBLY) value += L".asm";
        }
        return value;
    }
    return L"<unknown>";
}

std::wstring FeatureName(ProFeature* feature) {
    if (!feature) return L"<null>";
    ProName name;
    name[0] = L'\0';
    if (ProModelitemNameGet(reinterpret_cast<ProModelitem*>(feature), name) == PRO_TK_NO_ERROR)
        return FromWide(name);
    return L"<unnamed>";
}

std::wstring FeatureTypeName(ProFeature* feature) {
    if (!feature) return L"<null>";
    ProName typeName;
    typeName[0] = L'\0';
    if (ProFeatureTypenameGet(feature, typeName) == PRO_TK_NO_ERROR)
        return FromWide(typeName);

    ProFeattype type{};
    if (ProFeatureTypeGet(feature, &type) == PRO_TK_NO_ERROR) {
        std::wstringstream ss;
        ss << L"TYPE_" << static_cast<int>(type);
        return ss.str();
    }
    return L"<unknown>";
}

std::wstring ErrorName(ProError error) {
    switch (error) {
        case PRO_TK_NO_ERROR: return L"PRO_TK_NO_ERROR";
        case PRO_TK_BAD_INPUTS: return L"PRO_TK_BAD_INPUTS";
        case PRO_TK_GENERAL_ERROR: return L"PRO_TK_GENERAL_ERROR";
        case PRO_TK_E_NOT_FOUND: return L"PRO_TK_E_NOT_FOUND";
        case PRO_TK_E_FOUND: return L"PRO_TK_E_FOUND";
        case PRO_TK_INVALID_TYPE: return L"PRO_TK_INVALID_TYPE";
        case PRO_TK_UNAV_SEC: return L"PRO_TK_UNAV_SEC";
        case PRO_TK_CANT_MODIFY: return L"PRO_TK_CANT_MODIFY";
        case PRO_TK_USER_ABORT: return L"PRO_TK_USER_ABORT";
        case PRO_TK_CANT_OPEN: return L"PRO_TK_CANT_OPEN";
        case PRO_TK_INVALID_FILE: return L"PRO_TK_INVALID_FILE";
        case PRO_TK_NO_PERMISSION: return L"PRO_TK_NO_PERMISSION";
        case PRO_TK_NO_LICENSE: return L"PRO_TK_NO_LICENSE";
        case PRO_TK_UNSUPPORTED: return L"PRO_TK_UNSUPPORTED";
        default: {
            std::wstringstream ss;
            ss << L"ProError(" << static_cast<int>(error) << L")";
            return ss.str();
        }
    }
}

std::wstring StatusName(QcStatus status) {
    switch (status) {
        case QcStatus::Pass: return L"PASS";
        case QcStatus::Fail: return L"FAIL";
        case QcStatus::Warning: return L"WARNING";
        case QcStatus::Skip: return L"SKIP";
        case QcStatus::Error: return L"ERROR";
    }
    return L"UNKNOWN";
}

std::wstring InspectionStatusName(InspectionStatus status) {
    switch (status) {
        case InspectionStatus::Added: return L"ADDED";
        case InspectionStatus::Warning: return L"WARNING";
        case InspectionStatus::Failed: return L"FAILED";
        case InspectionStatus::Skipped: return L"SKIPPED";
    }
    return L"UNKNOWN";
}

std::wstring ModelTypeName(ProMdlType type) {
    switch (type) {
        case PRO_MDL_PART: return L"PART";
        case PRO_MDL_ASSEMBLY: return L"ASSEMBLY";
        default: return L"OTHER";
    }
}

std::wstring SourceTypeName(ModelSourceType type) {
    switch (type) {
        case ModelSourceType::CreoPart: return L"Creo Part";
        case ModelSourceType::CreoAssembly: return L"Creo Assembly";
        case ModelSourceType::CreoFamilyInstance: return L"Family Instance";
        case ModelSourceType::Step: return L"STEP";
    }
    return L"Unknown";
}

bool IsPart(ProMdl model) {
    if (!model) return false;
    ProMdlType type = PRO_MDL_UNUSED;
    return ProMdlTypeGet(model, &type) == PRO_TK_NO_ERROR && type == PRO_MDL_PART;
}

bool IsAssembly(ProMdl model) {
    if (!model) return false;
    ProMdlType type = PRO_MDL_UNUSED;
    return ProMdlTypeGet(model, &type) == PRO_TK_NO_ERROR && type == PRO_MDL_ASSEMBLY;
}

bool IsSolid(ProMdl model) { return IsPart(model) || IsAssembly(model); }

bool SameModel(ProMdl a, ProMdl b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return _wcsicmp(ModelName(a).c_str(), ModelName(b).c_str()) == 0;
}

ProError DisplayModel(ProMdl model) {
    if (!model) return PRO_TK_BAD_INPUTS;
    ProError error = ProMdlDisplay(model);
    if (error != PRO_TK_NO_ERROR && error != PRO_TK_GENERAL_ERROR)
        return error;

    int windowId = -1;
    if (ProMdlWindowGet(model, &windowId) == PRO_TK_NO_ERROR) {
        ProWindowCurrentSet(windowId);
        ProWindowRefit(windowId);
        ProWindowRefresh(windowId);
    }
    return PRO_TK_NO_ERROR;
}

ProError HighlightFeature(ProMdl model, int featureId) {
    if (!model || featureId < 0) return PRO_TK_BAD_INPUTS;
    ProError error = DisplayModel(model);
    if (error != PRO_TK_NO_ERROR) return error;

    ProFeature feature;
    error = ProFeatureInit(reinterpret_cast<ProSolid>(model), featureId, &feature);
    if (error != PRO_TK_NO_ERROR) return error;

    ProSelection selection = nullptr;
    error = ProSelectionAlloc(nullptr, reinterpret_cast<ProModelitem*>(&feature), &selection);
    if (error != PRO_TK_NO_ERROR) return error;

    error = ProSelectionHighlight(selection, PRO_COLOR_HIGHLITE);
    int windowId = -1;
    if (ProMdlWindowGet(model, &windowId) == PRO_TK_NO_ERROR)
        ProWindowRefresh(windowId);
    ProSelectionFree(&selection);
    return error;
}

ProError CurrentModel(ProMdl* model) {
    if (!model) return PRO_TK_BAD_INPUTS;
    return ProMdlCurrentGet(model);
}

bool CopyToProPath(const std::wstring& text, ProPath out) {
    if (!out || text.size() >= PRO_PATH_SIZE) return false;
    wcsncpy_s(out, PRO_PATH_SIZE, text.c_str(), _TRUNCATE);
    return true;
}

bool CopyToProMdlName(const std::wstring& text, ProMdlName out) {
    if (!out || text.size() >= PRO_MDLNAME_SIZE) return false;
    wcsncpy_s(out, PRO_MDLNAME_SIZE, text.c_str(), _TRUNCATE);
    return true;
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    return value;
}

} // namespace ModelUtils
