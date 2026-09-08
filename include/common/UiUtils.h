#pragma once

#include <ProToolkit.h>
#include <string>
#include <vector>

namespace UiUtils {
void MessageInfo(const char* key);
void MessageWarning(const char* key);
void MessageError(const char* key);
void MessageErrorWithToolkitCode(const char* key, ProError error);

void SetLabel(const char* dialog, const char* component, const std::wstring& text);
void SetInput(const char* dialog, const char* component, const std::wstring& text);
std::wstring GetInput(const char* dialog, const char* component);
void SetTextArea(const char* dialog, const char* component, const std::wstring& text);
std::wstring GetTextArea(const char* dialog, const char* component);
bool GetCheck(const char* dialog, const char* component, bool fallback = false);
void SetCheck(const char* dialog, const char* component, bool checked);
void EnableInput(const char* dialog, const char* component, bool enabled);
void EnableTextArea(const char* dialog, const char* component, bool enabled);
void EnableCheck(const char* dialog, const char* component, bool enabled);
void EnableButton(const char* dialog, const char* component, bool enabled);
void SetProgress(const char* dialog, const char* component, int value, int maxValue);
void SetRows(const char* dialog, const char* table, const std::vector<std::string>& rowNames);
void SetCell(const char* dialog, const char* table, const char* row, const char* column, const std::wstring& text);
int SelectedRowIndex(const char* dialog, const char* table, char prefix);
void SelectRow(const char* dialog, const char* table, const std::string& rowName);
}
