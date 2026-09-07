#pragma once

#include <string>

class FolderPicker {
public:
    static bool Pick(const std::wstring& title, const std::wstring& initialFolder, std::wstring& selectedFolder);
};
