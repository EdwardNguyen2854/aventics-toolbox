#pragma once

#include <string>

class Logger {
public:
    static void Initialize();
    static void Shutdown();
    static void Info(const std::wstring& message);
    static void Warn(const std::wstring& message);
    static void Error(const std::wstring& message);
    static std::wstring LogPath();
};
