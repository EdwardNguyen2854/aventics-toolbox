#include <ProToolkit.h>
#include "common/Logger.h"
#include "config/AppConfig.h"

#include <Windows.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace {
std::mutex g_mutex;
std::filesystem::path g_path;
bool g_ready = false;

std::filesystem::path LocalAppData() {
    size_t n = 0;
    _wgetenv_s(&n, nullptr, 0, L"LOCALAPPDATA");
    if (n > 1) {
        std::vector<wchar_t> b(n);
        _wgetenv_s(&n, b.data(), b.size(), L"LOCALAPPDATA");
        if (!b.empty() && b[0]) return b.data();
    }
    return std::filesystem::temp_directory_path();
}

std::filesystem::path InstallRoot() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&InstallRoot),
            &module) || !module) {
        return {};
    }

    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};

    const std::filesystem::path dllPath(std::wstring(path.data(), length));
    const auto binDir = dllPath.parent_path();
    if (binDir.empty()) return {};
    return binDir.parent_path();
}

std::wstring Now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &t);
    std::wstringstream ss;
    ss << std::put_time(&local, L"%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void Write(const wchar_t* level, const std::wstring& text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready) return;
    std::wofstream f(g_path, std::ios::app);
    if (f) f << Now() << L"  " << level << L"  " << text << L"\n";
}

bool TryInitializeAt(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    try {
        std::filesystem::create_directories(dir);
        const auto candidate = dir / AppConfig::LogFileName;
        std::wofstream f(candidate, std::ios::app);
        if (!f) return false;
        g_path = candidate;
        g_ready = true;
        f << Now() << L"  INFO  Aventics Toolbox started\n";
        f << Now() << L"  INFO  Native log: " << g_path.wstring() << L"\n";
        return true;
    } catch (...) {
        return false;
    }
}
}

void Logger::Initialize() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ready) return;

    const auto root = InstallRoot();
    if (!root.empty() && TryInitializeAt(root / L"logs")) return;

    const auto fallback = LocalAppData() / AppConfig::LogCompanyDir / AppConfig::LogProductDir / L"logs";
    TryInitializeAt(fallback);
}

void Logger::Shutdown() { Write(L"INFO", L"Aventics Toolbox stopped"); g_ready = false; }
void Logger::Info(const std::wstring& m) { Write(L"INFO", m); }
void Logger::Warn(const std::wstring& m) { Write(L"WARN", m); }
void Logger::Error(const std::wstring& m) { Write(L"ERROR", m); }
std::wstring Logger::LogPath() { return g_path.wstring(); }
