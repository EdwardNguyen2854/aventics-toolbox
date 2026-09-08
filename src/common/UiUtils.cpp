#include <ProToolkit.h>
#include "common/UiUtils.h"
#include "common/Logger.h"
#include "common/ModelUtils.h"

#include <ProArray.h>
#include <ProMessage.h>
#include <ProUtil.h>
#include <ProUILabel.h>
#include <ProUIInputpanel.h>
#include <ProUITextarea.h>
#include <ProUICheckbutton.h>
#include <ProUIProgressbar.h>
#include <ProUITable.h>
#include <ProUIPushbutton.h>

#include <algorithm>
#include <string>

namespace {
void Display(const char* key) {
    ProFileName file;
    ProStringToWstring(file, const_cast<char*>("aventics_messages.txt"));
    const ProError error = ProMessageDisplay(file, const_cast<char*>(key));
    if (error != PRO_TK_NO_ERROR) {
        std::wstring keyText;
        for (const char* p = key; p && *p; ++p) keyText.push_back(static_cast<wchar_t>(*p));
        Logger::Error(L"ProMessageDisplay failed for " + keyText + L": " + ModelUtils::ErrorName(error));
    }
}
}

namespace UiUtils {
void MessageInfo(const char* key) { Display(key); }
void MessageWarning(const char* key) { Display(key); }
void MessageError(const char* key) { Display(key); }
void MessageErrorWithToolkitCode(const char* key, ProError error) {
    Logger::Error(L"UI error: " + ModelUtils::ErrorName(error));
    Display(key);
}

void SetLabel(const char* dialog, const char* component, const std::wstring& text) {
    ProUILabelTextSet(const_cast<char*>(dialog), const_cast<char*>(component), const_cast<wchar_t*>(text.c_str()));
}

void SetInput(const char* dialog, const char* component, const std::wstring& text) {
    ProUIInputpanelValueSet(const_cast<char*>(dialog), const_cast<char*>(component), const_cast<wchar_t*>(text.c_str()));
}

std::wstring GetInput(const char* dialog, const char* component) {
    wchar_t* value = nullptr;
    if (ProUIInputpanelValueGet(const_cast<char*>(dialog), const_cast<char*>(component), &value) != PRO_TK_NO_ERROR || !value)
        return L"";
    std::wstring result(value);
    ProWstringFree(value);
    return result;
}

void SetTextArea(const char* dialog, const char* component, const std::wstring& text) {
    ProUITextareaValueSet(const_cast<char*>(dialog), const_cast<char*>(component), const_cast<wchar_t*>(text.c_str()));
}

std::wstring GetTextArea(const char* dialog, const char* component) {
    wchar_t* value = nullptr;
    if (ProUITextareaValueGet(const_cast<char*>(dialog), const_cast<char*>(component), &value) != PRO_TK_NO_ERROR || !value)
        return L"";
    std::wstring result(value);
    ProWstringFree(value);
    return result;
}

bool GetCheck(const char* dialog, const char* component, bool fallback) {
    ProBoolean state = fallback ? PRO_B_TRUE : PRO_B_FALSE;
    if (ProUICheckbuttonGetState(const_cast<char*>(dialog), const_cast<char*>(component), &state) != PRO_TK_NO_ERROR)
        return fallback;
    return state == PRO_B_TRUE;
}

void SetCheck(const char* dialog, const char* component, bool checked) {
    if (checked) ProUICheckbuttonSet(const_cast<char*>(dialog), const_cast<char*>(component));
    else ProUICheckbuttonUnset(const_cast<char*>(dialog), const_cast<char*>(component));
}

void EnableInput(const char* dialog, const char* component, bool enabled) {
    if (enabled) ProUIInputpanelEnable(const_cast<char*>(dialog), const_cast<char*>(component));
    else ProUIInputpanelDisable(const_cast<char*>(dialog), const_cast<char*>(component));
}

void EnableTextArea(const char* dialog, const char* component, bool enabled) {
    if (enabled) ProUITextareaEnable(const_cast<char*>(dialog), const_cast<char*>(component));
    else ProUITextareaDisable(const_cast<char*>(dialog), const_cast<char*>(component));
}

void EnableCheck(const char* dialog, const char* component, bool enabled) {
    if (enabled) ProUICheckbuttonEnable(const_cast<char*>(dialog), const_cast<char*>(component));
    else ProUICheckbuttonDisable(const_cast<char*>(dialog), const_cast<char*>(component));
}

void EnableButton(const char* dialog, const char* component, bool enabled) {
    if (enabled) ProUIPushbuttonEnable(const_cast<char*>(dialog), const_cast<char*>(component));
    else ProUIPushbuttonDisable(const_cast<char*>(dialog), const_cast<char*>(component));
}

void SetProgress(const char* dialog, const char* component, int value, int maxValue) {
    const int maxSafe = std::max(1, maxValue);
    ProUIProgressbarMinintegerSet(const_cast<char*>(dialog), const_cast<char*>(component), 0);
    ProUIProgressbarMaxintegerSet(const_cast<char*>(dialog), const_cast<char*>(component), maxSafe);
    ProUIProgressbarIntegerSet(const_cast<char*>(dialog), const_cast<char*>(component), std::clamp(value, 0, maxSafe));
}

void SetRows(const char* dialog, const char* table, const std::vector<std::string>& rowNames) {
    if (rowNames.empty()) {
        char* names[] = { const_cast<char*>("empty") };
        wchar_t* labels[] = { const_cast<wchar_t*>(L"") };
        ProUITableRownamesSet(const_cast<char*>(dialog), const_cast<char*>(table), 1, names);
        ProUITableRowlabelsSet(const_cast<char*>(dialog), const_cast<char*>(table), 1, labels);
        return;
    }

    std::vector<char*> names;
    std::vector<std::wstring> labelStorage(rowNames.size(), L"");
    std::vector<wchar_t*> labels;
    names.reserve(rowNames.size());
    labels.reserve(rowNames.size());
    for (const auto& r : rowNames) names.push_back(const_cast<char*>(r.c_str()));
    for (auto& l : labelStorage) labels.push_back(const_cast<wchar_t*>(l.c_str()));
    ProUITableRownamesSet(const_cast<char*>(dialog), const_cast<char*>(table), static_cast<int>(names.size()), names.data());
    ProUITableRowlabelsSet(const_cast<char*>(dialog), const_cast<char*>(table), static_cast<int>(labels.size()), labels.data());
}

void SetCell(const char* dialog, const char* table, const char* row, const char* column, const std::wstring& text) {
    ProUITableCellLabelSet(const_cast<char*>(dialog), const_cast<char*>(table),
                           const_cast<char*>(row), const_cast<char*>(column),
                           const_cast<wchar_t*>(text.c_str()));
}

int SelectedRowIndex(const char* dialog, const char* table, char prefix) {
    char** selected = nullptr;
    int count = 0;
    if (ProUITableSelectedrownamesGet(const_cast<char*>(dialog), const_cast<char*>(table), &count, &selected) != PRO_TK_NO_ERROR)
        return -1;

    int result = -1;
    for (int i = 0; i < count; ++i) {
        if (!selected[i]) continue;
        std::string row(selected[i]);
        if (row.size() > 1 && row[0] == prefix) {
            try { result = std::stoi(row.substr(1)); } catch (...) { result = -1; }
            break;
        }
    }
    if (selected) ProStringarrayFree(selected, count);
    return result;
}

void SelectRow(const char* dialog, const char* table, const std::string& rowName) {
    char* rows[] = { const_cast<char*>(rowName.c_str()) };
    ProUITableSelectedrownamesSet(const_cast<char*>(dialog), const_cast<char*>(table), 1, rows);
}
}
