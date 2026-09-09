#include <ProToolkit.h>
#include <ProMdl.h>
#include <ProObjects.h>

#include "app/WebToolbox.h"
#include "app/AppContext.h"
#include "common/FolderPicker.h"
#include "common/FolderScanner.h"
#include "common/Logger.h"
#include "common/ModelLoader.h"
#include "common/ModelUtils.h"
#include "common/SelectionService.h"
#include "common/SessionSnapshot.h"
#include "config/AppConfig.h"
#include "tools/AccuracyChecker.h"
#include "tools/InspectionBuilder.h"
#include "tools/InstanceBuilder.h"
#include "tools/WeakDimensionChecker.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <WebView2.h>
#include <wrl.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kWindowClass[] = L"AventicsToolboxWebViewWindow";
constexpr wchar_t kWindowTitle[] = L"Aventics Toolbox — TypeScript UI";
constexpr UINT_PTR kOperationTimer = 0x41565457; // AVTW

enum class WebOperation {
    None,
    RunAll,
    Weak,
    Accuracy,
    Inspection,
    InstanceBuilder
};

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

int HexDigit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

std::wstring UrlDecode(const std::wstring& encoded) {
    std::string bytes;
    bytes.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const wchar_t c = encoded[i];
        if (c == L'%' && i + 2 < encoded.size()) {
            const int hi = HexDigit(encoded[i + 1]);
            const int lo = HexDigit(encoded[i + 2]);
            if (hi >= 0 && lo >= 0) {
                bytes.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (c <= 0x7f) {
            bytes.push_back(static_cast<char>(c));
        } else {
            bytes += WideToUtf8(std::wstring(1, c));
        }
    }
    return Utf8ToWide(bytes);
}

std::vector<std::wstring> SplitProtocol(const std::wstring& message) {
    std::vector<std::wstring> fields;
    std::size_t start = 0;
    while (start <= message.size()) {
        const std::size_t end = message.find(L'\t', start);
        const std::wstring raw = message.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        fields.push_back(UrlDecode(raw));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return fields;
}

bool ParseBool(const std::wstring& value) {
    return value == L"1" || value == L"true" || value == L"True";
}

bool ParsePositiveInt(const std::wstring& text, int& value) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(text, &used);
        if (used != text.size() || parsed < 1) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseNonNegativeDouble(const std::wstring& text, double& value) {
    try {
        std::size_t used = 0;
        const double parsed = std::stod(text, &used);
        if (used != text.size() || !std::isfinite(parsed) || parsed < 0.0) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

std::string JsonString(const std::wstring& value) {
    const std::string utf8 = WideToUtf8(value);
    std::ostringstream out;
    out << '"';
    for (unsigned char c : utf8) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec;
            } else {
                out << static_cast<char>(c);
            }
        }
    }
    out << '"';
    return out.str();
}

const char* JsonBool(bool value) { return value ? "true" : "false"; }

std::wstring OperationName(WebOperation operation) {
    switch (operation) {
    case WebOperation::RunAll: return L"Run all QC";
    case WebOperation::Weak: return L"Weak dimensions";
    case WebOperation::Accuracy: return L"Accuracy";
    case WebOperation::Inspection: return L"Inspection";
    case WebOperation::InstanceBuilder: return L"Instance Builder";
    case WebOperation::None:
    default: return L"";
    }
}

FolderScanOptions WeakFolderOptions(const AppContext& context) {
    FolderScanOptions options;
    options.includeSubfolders = context.weakRecursive;
    options.latestCreoVersionOnly = context.weakLatest;
    options.includeParts = true;
    options.includeAssemblies = false;
    options.includeStep = false;
    return options;
}

FolderScanOptions AccuracyFolderOptions(const AppContext& context) {
    FolderScanOptions options;
    options.includeSubfolders = context.accuracyRecursive;
    options.latestCreoVersionOnly = context.accuracyLatest;
    options.includeParts = context.accuracyParts;
    options.includeAssemblies = context.accuracyAssemblies;
    options.includeStep = false;
    return options;
}

FolderScanOptions InspectionFolderOptions(const InspectionOptions& input) {
    FolderScanOptions options;
    options.includeSubfolders = input.includeSubfolders;
    options.latestCreoVersionOnly = input.latestCreoVersionOnly;
    options.includeParts = input.includeParts;
    options.includeAssemblies = input.includeAssemblies;
    options.includeStep = input.includeStep;
    return options;
}

FolderScanOptions InstanceFolderOptions(const InstanceBuilderOptions& input) {
    FolderScanOptions options;
    options.includeSubfolders = input.includeSubfolders;
    options.latestCreoVersionOnly = input.latestCreoVersionOnly;
    options.includeParts = true;
    options.includeAssemblies = true;
    options.includeStep = false;
    return options;
}

HMODULE CurrentModule() {
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CurrentModule),
        &module);
    return module;
}

std::filesystem::path ModuleDirectory() {
    wchar_t path[MAX_PATH] = {};
    const HMODULE module = CurrentModule();
    if (!module || !GetModuleFileNameW(module, path, MAX_PATH)) return {};
    return std::filesystem::path(path).parent_path();
}

std::wstring LocalWebViewDataDirectory() {
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    std::filesystem::path path;
    if (length > 0 && length < MAX_PATH) {
        path = std::filesystem::path(localAppData) / L"Aventics" / L"AventicsToolbox" / L"WebView2";
    } else {
        path = std::filesystem::temp_directory_path() / L"AventicsToolboxWebView2";
    }
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return path.wstring();
}

std::wstring FileUri(const std::filesystem::path& path) {
    wchar_t uri[4096] = {};
    DWORD length = static_cast<DWORD>(std::size(uri));
    if (SUCCEEDED(UrlCreateFromPathW(path.c_str(), uri, &length, 0))) return uri;
    return L"";
}

class WebToolboxHost {
public:
    static WebToolboxHost& Instance() {
        static WebToolboxHost host;
        return host;
    }

    ProError Show() {
        if (hwnd_) {
            ShowWindow(hwnd_, SW_SHOW);
            SetForegroundWindow(hwnd_);
            SendState();
            return PRO_TK_NO_ERROR;
        }

        const auto moduleDir = ModuleDirectory();
        uiIndexPath_ = moduleDir / L"ui" / L"index.html";
        if (moduleDir.empty() || !std::filesystem::exists(uiIndexPath_)) {
            Logger::Error(L"TypeScript UI asset was not found beside the TOOLKIT DLL: " + uiIndexPath_.wstring());
            return PRO_TK_GENERAL_ERROR;
        }

        loaderModule_ = LoadLibraryW((moduleDir / L"WebView2Loader.dll").c_str());
        if (!loaderModule_) loaderModule_ = LoadLibraryW(L"WebView2Loader.dll");
        if (!loaderModule_) {
            Logger::Error(L"WebView2Loader.dll was not found. Falling back to the native Creo UI is recommended.");
            return PRO_TK_GENERAL_ERROR;
        }

        createEnvironment_ = reinterpret_cast<CreateEnvironmentFn>(
            GetProcAddress(loaderModule_, "CreateCoreWebView2EnvironmentWithOptions"));
        if (!createEnvironment_) {
            Logger::Error(L"WebView2Loader.dll does not expose CreateCoreWebView2EnvironmentWithOptions.");
            FreeLibrary(loaderModule_);
            loaderModule_ = nullptr;
            return PRO_TK_GENERAL_ERROR;
        }

        if (!RegisterWindowClass()) return PRO_TK_GENERAL_ERROR;

        hwnd_ = CreateWindowExW(
            0,
            kWindowClass,
            kWindowTitle,
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1240,
            800,
            nullptr,
            nullptr,
            CurrentModule(),
            nullptr);
        if (!hwnd_) {
            Logger::Error(L"Could not create the WebView2 toolbox host window.");
            return PRO_TK_GENERAL_ERROR;
        }

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        statusMessage_ = L"Starting TypeScript UI...";

        const HRESULT hr = createEnvironment_(
            nullptr,
            LocalWebViewDataDirectory().c_str(),
            nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                    if (FAILED(result) || !environment || !hwnd_) {
                        Logger::Error(L"Could not initialize the WebView2 environment.");
                        if (hwnd_)
                            MessageBoxW(hwnd_, L"WebView2 could not initialize. Check the WebView2 Runtime installation and toolbox log.",
                                        L"Aventics Toolbox", MB_OK | MB_ICONERROR);
                        return S_OK;
                    }
                    environment_ = environment;
                    environment_->CreateCoreWebView2Controller(
                        hwnd_,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                                if (FAILED(controllerResult) || !controller || !hwnd_) {
                                    Logger::Error(L"Could not create the WebView2 controller.");
                                    return S_OK;
                                }
                                controller_ = controller;
                                controller_->get_CoreWebView2(&webView_);
                                if (!webView_) {
                                    Logger::Error(L"WebView2 controller did not return a browser instance.");
                                    return S_OK;
                                }

                                ComPtr<ICoreWebView2Settings> settings;
                                if (SUCCEEDED(webView_->get_Settings(&settings)) && settings) {
                                    settings->put_IsWebMessageEnabled(TRUE);
                                    settings->put_AreDefaultContextMenusEnabled(FALSE);
                                    settings->put_IsStatusBarEnabled(FALSE);
                                }

                                webView_->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            LPWSTR raw = nullptr;
                                            if (args && SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                                HandleProtocol(raw);
                                                CoTaskMemFree(raw);
                                            }
                                            return S_OK;
                                        }).Get(),
                                    &webMessageToken_);

                                ResizeWebView();
                                const std::wstring uri = FileUri(uiIndexPath_);
                                if (uri.empty() || FAILED(webView_->Navigate(uri.c_str()))) {
                                    Logger::Error(L"Could not navigate WebView2 to the TypeScript UI entry point.");
                                }
                                return S_OK;
                            }).Get());
                    return S_OK;
                }).Get());

        if (FAILED(hr)) {
            Logger::Error(L"CreateCoreWebView2EnvironmentWithOptions failed before asynchronous initialization.");
            DestroyWindow(hwnd_);
            return PRO_TK_GENERAL_ERROR;
        }

        Logger::Info(L"Opened experimental TypeScript/WebView2 toolbox UI.");
        return PRO_TK_NO_ERROR;
    }

    void Shutdown() {
        StopOperationWithoutCallbacks();
        if (hwnd_) DestroyWindow(hwnd_);
        webView_.Reset();
        controller_.Reset();
        environment_.Reset();
        if (loaderModule_) {
            FreeLibrary(loaderModule_);
            loaderModule_ = nullptr;
        }
        createEnvironment_ = nullptr;
    }

private:
    using CreateEnvironmentFn = HRESULT(STDAPICALLTYPE*)(
        PCWSTR,
        PCWSTR,
        ICoreWebView2EnvironmentOptions*,
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

    WebToolboxHost() = default;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        return Instance().HandleWindowMessage(hwnd, message, wParam, lParam);
    }

    bool RegisterWindowClass() {
        static bool registered = false;
        if (registered) return true;
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = CurrentModule();
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            Logger::Error(L"Could not register the WebView2 toolbox host window class.");
            return false;
        }
        registered = true;
        return true;
    }

    LRESULT HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_SIZE:
            ResizeWebView();
            return 0;
        case WM_TIMER:
            if (wParam == kOperationTimer) {
                KillTimer(hwnd, kOperationTimer);
                TickOperation();
                return 0;
            }
            break;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 860;
            info->ptMinTrackSize.y = 580;
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (hwnd == hwnd_) {
                StopOperationWithoutCallbacks();
                if (controller_) controller_->Close();
                webView_.Reset();
                controller_.Reset();
                environment_.Reset();
                hwnd_ = nullptr;
            }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void ResizeWebView() {
        if (!hwnd_ || !controller_) return;
        RECT bounds{};
        GetClientRect(hwnd_, &bounds);
        controller_->put_Bounds(bounds);
    }

    void ToggleMaximize() {
        if (!hwnd_) return;
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
    }

    void HideForCreoInteraction() {
        if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    }

    void RestoreAfterCreoInteraction() {
        if (!hwnd_) return;
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
    }

    void HandleProtocol(const std::wstring& rawMessage) {
        const auto fields = SplitProtocol(rawMessage);
        if (fields.empty()) return;
        const std::wstring& action = fields[0];

        if (action == L"ready" || action == L"refresh") {
            statusMessage_ = busy_ ? statusMessage_ : L"Ready";
            SendState();
            return;
        }
        if (action == L"toggleMaximize") {
            ToggleMaximize();
            return;
        }
        if (action == L"cancel") {
            if (busy_) FinishOperation(true);
            return;
        }
        if (action == L"pickFolder" && fields.size() >= 3) {
            PickFolder(fields[1], fields[2]);
            return;
        }
        if (action == L"clear" && fields.size() >= 2) {
            ClearResults(fields[1]);
            return;
        }
        if (action == L"runAll") {
            RunAll();
            return;
        }
        if (action == L"runWeak") {
            RunWeak(fields);
            return;
        }
        if (action == L"runAccuracy") {
            RunAccuracy(fields);
            return;
        }
        if (action == L"runInspection") {
            RunInspection(fields);
            return;
        }
        if (action == L"planInstances") {
            PlanInstances(fields);
            return;
        }
        if (action == L"runInstances") {
            RunInstances(fields);
            return;
        }
        if (action == L"exportInstances") {
            if (busy_) return;
            std::wstring message;
            InstanceBuilder::ExportCsv(AppContext::Instance().instanceResults, message);
            statusMessage_ = message;
            SendState();
            return;
        }

        statusMessage_ = L"Unknown UI action: " + action;
        SendState();
    }

    void PickFolder(const std::wstring& tool, const std::wstring& current) {
        if (busy_) return;
        std::wstring title = L"Select folder";
        if (tool == L"weak") title = L"Select folder for Weak Dimension check";
        else if (tool == L"accuracy") title = L"Select folder for Accuracy check";
        else if (tool == L"inspection") title = L"Select folder for Inspection Assembly Builder";
        else if (tool == L"instances") title = L"Select folder containing Creo generics for Instance Builder";

        std::wstring selected;
        HideForCreoInteraction();
        const bool picked = FolderPicker::Pick(title, current, selected);
        RestoreAfterCreoInteraction();
        if (!picked) {
            statusMessage_ = L"Folder selection cancelled.";
            SendState();
            return;
        }

        auto& context = AppContext::Instance();
        if (tool == L"weak") context.weakFolder = selected;
        else if (tool == L"accuracy") context.accuracyFolder = selected;
        else if (tool == L"inspection") context.inspectionFolder = selected;
        else if (tool == L"instances") context.instanceFolder = selected;
        statusMessage_ = L"Folder selected.";
        SendState();
    }

    void ClearResults(const std::wstring& tool) {
        if (busy_) return;
        auto& context = AppContext::Instance();
        if (tool == L"weak") context.weakResults.clear();
        else if (tool == L"accuracy") context.accuracyResults.clear();
        else if (tool == L"inspection") context.inspectionResults.clear();
        else if (tool == L"instances") context.instanceResults.clear();
        statusMessage_ = L"Results cleared.";
        SendState();
    }

    void RunAll() {
        if (busy_) return;
        std::vector<ModelDescriptor> models;
        HideForCreoInteraction();
        const ProError error = SelectionService::SelectModels(true, true, models);
        RestoreAfterCreoInteraction();
        if (error == PRO_TK_USER_ABORT) {
            statusMessage_ = L"Selection cancelled. Previous results were kept.";
            SendState();
            return;
        }
        if (error != PRO_TK_NO_ERROR || models.empty()) {
            statusMessage_ = error == PRO_TK_NO_ERROR ? L"No models selected." : L"Selection failed: " + ModelUtils::ErrorName(error);
            SendState();
            return;
        }
        auto& context = AppContext::Instance();
        context.weakResults.clear();
        context.accuracyResults.clear();
        sources_ = std::move(models);
        cleanupLoadedModels_ = false;
        StartOperation(WebOperation::RunAll);
    }

    void RunWeak(const std::vector<std::wstring>& fields) {
        // runWeak | useSelection | folder | recursive | latest
        if (busy_ || fields.size() < 5) return;
        auto& context = AppContext::Instance();
        context.weakUseSelection = ParseBool(fields[1]);
        context.weakFolder = fields[2];
        context.weakRecursive = ParseBool(fields[3]);
        context.weakLatest = ParseBool(fields[4]);

        std::vector<ModelDescriptor> models;
        if (context.weakUseSelection) {
            HideForCreoInteraction();
            const ProError error = SelectionService::SelectModels(true, false, models);
            RestoreAfterCreoInteraction();
            if (error == PRO_TK_USER_ABORT) {
                statusMessage_ = L"Selection cancelled. Previous results were kept.";
                SendState();
                return;
            }
            if (error != PRO_TK_NO_ERROR || models.empty()) {
                statusMessage_ = error == PRO_TK_NO_ERROR ? L"No parts selected." : L"Selection failed: " + ModelUtils::ErrorName(error);
                SendState();
                return;
            }
            cleanupLoadedModels_ = false;
        } else {
            const auto scan = FolderScanner::Discover(context.weakFolder, WeakFolderOptions(context));
            if (!scan.error.empty() || scan.models.empty()) {
                statusMessage_ = scan.error.empty() ? L"No eligible parts found in this folder." : scan.error;
                SendState();
                return;
            }
            models = scan.models;
            cleanupLoadedModels_ = true;
        }

        context.weakResults.clear();
        sources_ = std::move(models);
        StartOperation(WebOperation::Weak);
    }

    void RunAccuracy(const std::vector<std::wstring>& fields) {
        // runAccuracy | useSelection | folder | recursive | latest | parts | assemblies
        if (busy_ || fields.size() < 7) return;
        auto& context = AppContext::Instance();
        context.accuracyUseSelection = ParseBool(fields[1]);
        context.accuracyFolder = fields[2];
        context.accuracyRecursive = ParseBool(fields[3]);
        context.accuracyLatest = ParseBool(fields[4]);
        context.accuracyParts = ParseBool(fields[5]);
        context.accuracyAssemblies = ParseBool(fields[6]);
        if (!context.accuracyParts && !context.accuracyAssemblies) {
            statusMessage_ = L"Select Parts, Assemblies, or both before checking accuracy.";
            SendState();
            return;
        }

        std::vector<ModelDescriptor> models;
        if (context.accuracyUseSelection) {
            HideForCreoInteraction();
            const ProError error = SelectionService::SelectModels(context.accuracyParts, context.accuracyAssemblies, models);
            RestoreAfterCreoInteraction();
            if (error == PRO_TK_USER_ABORT) {
                statusMessage_ = L"Selection cancelled. Previous results were kept.";
                SendState();
                return;
            }
            if (error != PRO_TK_NO_ERROR || models.empty()) {
                statusMessage_ = error == PRO_TK_NO_ERROR ? L"No matching models selected." : L"Selection failed: " + ModelUtils::ErrorName(error);
                SendState();
                return;
            }
            cleanupLoadedModels_ = false;
        } else {
            const auto scan = FolderScanner::Discover(context.accuracyFolder, AccuracyFolderOptions(context));
            if (!scan.error.empty() || scan.models.empty()) {
                statusMessage_ = scan.error.empty() ? L"No eligible models found in this folder." : scan.error;
                SendState();
                return;
            }
            models = scan.models;
            cleanupLoadedModels_ = true;
        }

        context.accuracyResults.clear();
        sources_ = std::move(models);
        StartOperation(WebOperation::Accuracy);
    }

    void RunInspection(const std::vector<std::wstring>& fields) {
        // runInspection | folder | recursive | latest | parts | assemblies | family | generic | step |
        // autoArrange | rowsAlongX | useZ | columns | gap
        if (busy_ || fields.size() < 14) return;
        ProMdl current = nullptr;
        if (ModelUtils::CurrentModel(&current) != PRO_TK_NO_ERROR || !ModelUtils::IsAssembly(current)) {
            statusMessage_ = L"Open or create an assembly before adding inspection components.";
            SendState();
            return;
        }

        InspectionOptions options;
        options.includeSubfolders = ParseBool(fields[2]);
        options.latestCreoVersionOnly = ParseBool(fields[3]);
        options.includeParts = ParseBool(fields[4]);
        options.includeAssemblies = ParseBool(fields[5]);
        options.includeFamilyInstances = ParseBool(fields[6]);
        options.includeGenericWhenFamilyExists = ParseBool(fields[7]);
        options.includeStep = ParseBool(fields[8]);
        options.autoArrange = ParseBool(fields[9]);
        options.arrangeRowsAlongX = ParseBool(fields[10]);
        options.useZAxisForRows = ParseBool(fields[11]);
        if (!ParsePositiveInt(fields[12], options.columns)) {
            statusMessage_ = L"Columns must be a whole number of 1 or greater.";
            SendState();
            return;
        }
        if (!ParseNonNegativeDouble(fields[13], options.gap)) {
            statusMessage_ = L"Gap must be a finite number of 0 or greater.";
            SendState();
            return;
        }
        if (!options.includeParts && !options.includeAssemblies && !options.includeStep) {
            statusMessage_ = L"Select at least one source type: Creo parts, assemblies, or STEP.";
            SendState();
            return;
        }

        auto& context = AppContext::Instance();
        context.inspectionFolder = fields[1];
        context.inspectionRecursive = options.includeSubfolders;
        context.inspectionLatest = options.latestCreoVersionOnly;
        context.inspectionParts = options.includeParts;
        context.inspectionAssemblies = options.includeAssemblies;
        context.inspectionFamilyInstances = options.includeFamilyInstances;
        context.inspectionIncludeGeneric = options.includeGenericWhenFamilyExists;
        context.inspectionStep = options.includeStep;
        context.inspectionAutoArrange = options.autoArrange;
        context.inspectionRowsAlongX = options.arrangeRowsAlongX;
        context.inspectionUseZAxis = options.useZAxisForRows;
        context.inspectionColumns = options.columns;
        context.inspectionGap = options.gap;

        const auto scan = FolderScanner::Discover(context.inspectionFolder, InspectionFolderOptions(options));
        if (!scan.error.empty() || scan.models.empty()) {
            statusMessage_ = scan.error.empty() ? L"No eligible source files found in this folder." : scan.error;
            SendState();
            return;
        }

        context.inspectionResults.clear();
        sources_ = scan.models;
        inspectionBuilder_ = std::make_unique<InspectionBuilder>(reinterpret_cast<ProAssembly>(current), options);
        StartOperation(WebOperation::Inspection);
    }

    void PlanInstances(const std::vector<std::wstring>& fields) {
        // planInstances | codes | columns
        if (busy_ || fields.size() < 3) return;
        int columns = 0;
        if (!ParsePositiveInt(fields[2], columns)) {
            statusMessage_ = L"Columns must be a whole number of 1 or greater.";
            SendState();
            return;
        }
        std::vector<InstanceRequest> requests;
        std::wstring error;
        if (!InstanceBuilder::ParseRequests(fields[1], columns, requests, error)) {
            statusMessage_ = error;
            SendState();
            return;
        }

        auto& context = AppContext::Instance();
        context.instanceCodes = fields[1];
        context.instanceColumns = columns;
        context.instanceResults.clear();
        context.instanceResults.reserve(requests.size());
        for (const auto& request : requests) {
            InstanceBuildResult result;
            result.requestedCode = request.code;
            result.row = request.row;
            result.column = request.column;
            result.status = InstanceBuildStatus::Planned;
            result.details = L"Allocated; exact instance not searched yet.";
            context.instanceResults.push_back(std::move(result));
        }
        statusMessage_ = L"Instance positions planned. Review allocation before building.";
        SendState();
    }

    void RunInstances(const std::vector<std::wstring>& fields) {
        // runInstances | folder | recursive | latest | codes | columns | gap
        if (busy_ || fields.size() < 7) return;
        ProMdl current = nullptr;
        if (ModelUtils::CurrentModel(&current) != PRO_TK_NO_ERROR || !ModelUtils::IsAssembly(current)) {
            statusMessage_ = L"Open or create an assembly before building instances.";
            SendState();
            return;
        }

        InstanceBuilderOptions options;
        options.includeSubfolders = ParseBool(fields[2]);
        options.latestCreoVersionOnly = ParseBool(fields[3]);
        if (!ParsePositiveInt(fields[5], options.columns)) {
            statusMessage_ = L"Columns must be a whole number of 1 or greater.";
            SendState();
            return;
        }
        if (!ParseNonNegativeDouble(fields[6], options.gap)) {
            statusMessage_ = L"Gap must be a finite number of 0 or greater.";
            SendState();
            return;
        }

        std::vector<InstanceRequest> requests;
        std::wstring error;
        if (!InstanceBuilder::ParseRequests(fields[4], options.columns, requests, error)) {
            statusMessage_ = error;
            SendState();
            return;
        }
        if (fields[1].empty()) {
            statusMessage_ = L"Choose a source folder before building instances.";
            SendState();
            return;
        }

        auto& context = AppContext::Instance();
        context.instanceFolder = fields[1];
        context.instanceRecursive = options.includeSubfolders;
        context.instanceLatest = options.latestCreoVersionOnly;
        context.instanceCodes = fields[4];
        context.instanceColumns = options.columns;
        context.instanceGap = options.gap;

        const auto scan = FolderScanner::Discover(context.instanceFolder, InstanceFolderOptions(options));
        if (!scan.error.empty() || scan.models.empty()) {
            statusMessage_ = scan.error.empty() ? L"No Creo parts or assemblies were found in the selected folder." : scan.error;
            SendState();
            return;
        }

        context.instanceResults.clear();
        for (const auto& request : requests) {
            InstanceBuildResult result;
            result.requestedCode = request.code;
            result.row = request.row;
            result.column = request.column;
            result.status = InstanceBuildStatus::Planned;
            result.details = L"Searching...";
            context.instanceResults.push_back(std::move(result));
        }

        sources_ = scan.models;
        instanceBuilder_ = std::make_unique<InstanceBuilder>(
            reinterpret_cast<ProAssembly>(current), options, std::move(requests));
        StartOperation(WebOperation::InstanceBuilder);
    }

    void StartOperation(WebOperation operation) {
        operation_ = operation;
        operationIndex_ = 0;
        busy_ = true;
        statusMessage_ = OperationName(operation_) + L" | 0 / " + std::to_wstring(sources_.size());
        SendState();
        ScheduleTick();
    }

    void ScheduleTick() {
        if (!busy_ || !hwnd_) return;
        SetTimer(hwnd_, kOperationTimer, static_cast<UINT>(std::max(1, AppConfig::OperationTimerDelayMs)), nullptr);
    }

    void TickOperation() {
        if (!busy_) return;
        if (operationIndex_ >= sources_.size()) {
            FinishOperation(false);
            return;
        }

        auto& context = AppContext::Instance();
        const ModelDescriptor sourceCopy = sources_[operationIndex_];
        statusMessage_ = OperationName(operation_) + L" | " + sourceCopy.displayName;

        try {
            switch (operation_) {
            case WebOperation::RunAll:
                ProcessRunAllSource(sourceCopy);
                break;
            case WebOperation::Weak:
                ProcessWeakSource(sourceCopy);
                break;
            case WebOperation::Accuracy:
                ProcessAccuracySource(sourceCopy);
                break;
            case WebOperation::Inspection:
                if (inspectionBuilder_) inspectionBuilder_->Process(sourceCopy, context.inspectionResults);
                break;
            case WebOperation::InstanceBuilder:
                if (instanceBuilder_) instanceBuilder_->ProcessSource(sourceCopy);
                break;
            case WebOperation::None:
            default:
                break;
            }
        } catch (...) {
            Logger::Error(L"Unhandled exception inside experimental WebView operation step.");
        }

        ++operationIndex_;
        statusMessage_ = OperationName(operation_) + L" | " + std::to_wstring(operationIndex_) + L" / " +
                         std::to_wstring(sources_.size());
        if (operation_ == WebOperation::InstanceBuilder && instanceBuilder_) {
            statusMessage_ += L" | " + std::to_wstring(instanceBuilder_->ResolvedCount()) + L" / " +
                              std::to_wstring(context.instanceResults.size()) + L" codes resolved";
        }
        SendState();

        if (operationIndex_ >= sources_.size()) FinishOperation(false);
        else ScheduleTick();
    }

    void ProcessRunAllSource(ModelDescriptor source) {
        auto& context = AppContext::Instance();
        SessionSnapshot before = SessionSnapshot::Capture();
        const ProError loadError = ModelLoader::Load(source, before);
        if (loadError != PRO_TK_NO_ERROR || !source.model) {
            if (source.modelType == PRO_MDL_PART) {
                WeakResult weak;
                weak.modelName = source.displayName;
                weak.sourcePath = source.sourcePath;
                weak.status = QcStatus::Error;
                weak.errors = 1;
                weak.summary = L"Load failed: " + ModelUtils::ErrorName(loadError);
                context.weakResults.push_back(std::move(weak));
            }
            AccuracyResult accuracy;
            accuracy.modelName = source.displayName;
            accuracy.sourcePath = source.sourcePath;
            accuracy.modelType = source.modelType;
            accuracy.status = QcStatus::Error;
            accuracy.details = L"Load failed: " + ModelUtils::ErrorName(loadError);
            context.accuracyResults.push_back(std::move(accuracy));
            return;
        }
        if (ModelUtils::IsPart(source.model)) {
            WeakResult weak = WeakDimensionChecker().Run(source.model);
            weak.sourcePath = source.sourcePath;
            context.weakResults.push_back(std::move(weak));
        }
        AccuracyResult accuracy = AccuracyChecker().Run(source.model);
        accuracy.sourcePath = source.sourcePath;
        context.accuracyResults.push_back(std::move(accuracy));
    }

    void ProcessWeakSource(ModelDescriptor source) {
        auto& context = AppContext::Instance();
        SessionSnapshot before = SessionSnapshot::Capture();
        const ProError loadError = ModelLoader::Load(source, before);
        if (loadError != PRO_TK_NO_ERROR || !source.model) {
            WeakResult result;
            result.modelName = source.displayName;
            result.sourcePath = source.sourcePath;
            result.status = QcStatus::Error;
            result.errors = 1;
            result.summary = L"Load failed: " + ModelUtils::ErrorName(loadError);
            context.weakResults.push_back(std::move(result));
        } else {
            WeakResult result = WeakDimensionChecker().Run(source.model);
            result.sourcePath = source.sourcePath;
            if (cleanupLoadedModels_) result.model = nullptr;
            context.weakResults.push_back(std::move(result));
        }
        if (cleanupLoadedModels_) before.CleanupNewModels();
    }

    void ProcessAccuracySource(ModelDescriptor source) {
        auto& context = AppContext::Instance();
        SessionSnapshot before = SessionSnapshot::Capture();
        const ProError loadError = ModelLoader::Load(source, before);
        if (loadError != PRO_TK_NO_ERROR || !source.model) {
            AccuracyResult result;
            result.modelName = source.displayName;
            result.sourcePath = source.sourcePath;
            result.modelType = source.modelType;
            result.status = QcStatus::Error;
            result.details = L"Load failed: " + ModelUtils::ErrorName(loadError);
            context.accuracyResults.push_back(std::move(result));
        } else {
            AccuracyResult result = AccuracyChecker().Run(source.model);
            result.sourcePath = source.sourcePath;
            if (cleanupLoadedModels_) result.model = nullptr;
            context.accuracyResults.push_back(std::move(result));
        }
        if (cleanupLoadedModels_) before.CleanupNewModels();
    }

    void FinishOperation(bool cancelled) {
        if (!busy_) return;
        if (hwnd_) KillTimer(hwnd_, kOperationTimer);

        auto& context = AppContext::Instance();
        const WebOperation completed = operation_;
        if (completed == WebOperation::InstanceBuilder && !cancelled && instanceBuilder_) {
            instanceBuilder_->Finalize(context.instanceResults);
        }
        inspectionBuilder_.reset();
        instanceBuilder_.reset();
        sources_.clear();
        busy_ = false;
        operation_ = WebOperation::None;
        operationIndex_ = 0;
        cleanupLoadedModels_ = false;

        if (cancelled) {
            if (completed == WebOperation::Inspection)
                statusMessage_ = L"Inspection cancelled. Components already added remain; the assembly was not saved.";
            else if (completed == WebOperation::InstanceBuilder)
                statusMessage_ = L"Instance Builder cancelled before assembly. No new instance components were added.";
            else
                statusMessage_ = OperationName(completed) + L" cancelled. Partial results were kept.";
        } else {
            switch (completed) {
            case WebOperation::Inspection:
                statusMessage_ = L"Inspection completed. The assembly was not saved automatically.";
                break;
            case WebOperation::InstanceBuilder:
                statusMessage_ = L"Instance Builder completed. Review unresolved requests or export the CSV report.";
                break;
            case WebOperation::RunAll:
                statusMessage_ = L"Run all QC completed.";
                break;
            case WebOperation::Weak:
                statusMessage_ = L"Weak-dimension check completed.";
                break;
            case WebOperation::Accuracy:
                statusMessage_ = L"Accuracy check completed.";
                break;
            case WebOperation::None:
            default:
                statusMessage_ = L"Ready";
                break;
            }
        }
        SendState();
    }

    void StopOperationWithoutCallbacks() {
        if (hwnd_) KillTimer(hwnd_, kOperationTimer);
        sources_.clear();
        inspectionBuilder_.reset();
        instanceBuilder_.reset();
        busy_ = false;
        operation_ = WebOperation::None;
        operationIndex_ = 0;
        cleanupLoadedModels_ = false;
    }

    std::string BuildStateJson() const {
        const auto& context = AppContext::Instance();
        ProMdl current = nullptr;
        const bool hasModel = ModelUtils::CurrentModel(&current) == PRO_TK_NO_ERROR && current;
        const bool activeAssembly = hasModel && ModelUtils::IsAssembly(current);
        const std::wstring activeName = hasModel ? ModelUtils::ModelName(current) : L"";

        std::ostringstream out;
        out << "{\"type\":\"state\"";
        out << ",\"busy\":" << JsonBool(busy_);
        out << ",\"operation\":" << JsonString(OperationName(operation_));
        out << ",\"progress\":{\"done\":" << operationIndex_ << ",\"total\":" << sources_.size()
            << ",\"message\":" << JsonString(statusMessage_) << "}";
        out << ",\"activeModel\":{\"name\":" << JsonString(activeName)
            << ",\"isAssembly\":" << JsonBool(activeAssembly) << "}";

        out << ",\"settings\":{";
        out << "\"weak\":{\"folder\":" << JsonString(context.weakFolder)
            << ",\"useSelection\":" << JsonBool(context.weakUseSelection)
            << ",\"recursive\":" << JsonBool(context.weakRecursive)
            << ",\"latest\":" << JsonBool(context.weakLatest) << "},";
        out << "\"accuracy\":{\"folder\":" << JsonString(context.accuracyFolder)
            << ",\"useSelection\":" << JsonBool(context.accuracyUseSelection)
            << ",\"recursive\":" << JsonBool(context.accuracyRecursive)
            << ",\"latest\":" << JsonBool(context.accuracyLatest)
            << ",\"parts\":" << JsonBool(context.accuracyParts)
            << ",\"assemblies\":" << JsonBool(context.accuracyAssemblies) << "},";
        out << "\"inspection\":{\"folder\":" << JsonString(context.inspectionFolder)
            << ",\"recursive\":" << JsonBool(context.inspectionRecursive)
            << ",\"latest\":" << JsonBool(context.inspectionLatest)
            << ",\"parts\":" << JsonBool(context.inspectionParts)
            << ",\"assemblies\":" << JsonBool(context.inspectionAssemblies)
            << ",\"family\":" << JsonBool(context.inspectionFamilyInstances)
            << ",\"generic\":" << JsonBool(context.inspectionIncludeGeneric)
            << ",\"step\":" << JsonBool(context.inspectionStep)
            << ",\"autoArrange\":" << JsonBool(context.inspectionAutoArrange)
            << ",\"rowsAlongX\":" << JsonBool(context.inspectionRowsAlongX)
            << ",\"useZ\":" << JsonBool(context.inspectionUseZAxis)
            << ",\"columns\":" << context.inspectionColumns
            << ",\"gap\":" << context.inspectionGap << "},";
        out << "\"instances\":{\"folder\":" << JsonString(context.instanceFolder)
            << ",\"codes\":" << JsonString(context.instanceCodes)
            << ",\"recursive\":" << JsonBool(context.instanceRecursive)
            << ",\"latest\":" << JsonBool(context.instanceLatest)
            << ",\"columns\":" << context.instanceColumns
            << ",\"gap\":" << context.instanceGap << "}";
        out << "}";

        out << ",\"weakResults\":[";
        for (std::size_t i = 0; i < context.weakResults.size(); ++i) {
            if (i) out << ',';
            const auto& result = context.weakResults[i];
            out << "{\"modelName\":" << JsonString(result.modelName)
                << ",\"sourcePath\":" << JsonString(result.sourcePath)
                << ",\"status\":" << JsonString(ModelUtils::StatusName(result.status))
                << ",\"summary\":" << JsonString(result.summary)
                << ",\"weakDimensions\":" << result.weakDimensions
                << ",\"warnings\":" << result.warnings
                << ",\"errors\":" << result.errors
                << ",\"findings\":[";
            for (std::size_t j = 0; j < result.findings.size(); ++j) {
                if (j) out << ',';
                const auto& finding = result.findings[j];
                out << "{\"status\":" << JsonString(ModelUtils::StatusName(finding.status))
                    << ",\"featureId\":" << finding.featureId
                    << ",\"featureName\":" << JsonString(finding.featureName)
                    << ",\"featureType\":" << JsonString(finding.featureType)
                    << ",\"sectionIndex\":" << finding.sectionIndex
                    << ",\"dimensionId\":" << finding.dimensionId
                    << ",\"details\":" << JsonString(finding.details) << "}";
            }
            out << "]}";
        }
        out << "]";

        out << ",\"accuracyResults\":[";
        for (std::size_t i = 0; i < context.accuracyResults.size(); ++i) {
            if (i) out << ',';
            const auto& result = context.accuracyResults[i];
            out << "{\"modelName\":" << JsonString(result.modelName)
                << ",\"sourcePath\":" << JsonString(result.sourcePath)
                << ",\"kind\":" << JsonString(ModelUtils::ModelTypeName(result.modelType))
                << ",\"status\":" << JsonString(ModelUtils::StatusName(result.status))
                << ",\"accuracyType\":" << JsonString(result.accuracyType)
                << ",\"hasValue\":" << JsonBool(result.hasValue)
                << ",\"accuracyValue\":" << result.accuracyValue
                << ",\"details\":" << JsonString(result.details) << "}";
        }
        out << "]";

        out << ",\"inspectionResults\":[";
        for (std::size_t i = 0; i < context.inspectionResults.size(); ++i) {
            if (i) out << ',';
            const auto& result = context.inspectionResults[i];
            out << "{\"sourceName\":" << JsonString(result.sourceName)
                << ",\"sourcePath\":" << JsonString(result.sourcePath)
                << ",\"addedModelName\":" << JsonString(result.addedModelName)
                << ",\"sourceKind\":" << JsonString(result.sourceKind)
                << ",\"status\":" << JsonString(ModelUtils::InspectionStatusName(result.status))
                << ",\"featureId\":" << result.componentFeatureId
                << ",\"details\":" << JsonString(result.details) << "}";
        }
        out << "]";

        out << ",\"instanceResults\":[";
        for (std::size_t i = 0; i < context.instanceResults.size(); ++i) {
            if (i) out << ',';
            const auto& result = context.instanceResults[i];
            out << "{\"code\":" << JsonString(result.requestedCode)
                << ",\"row\":" << result.row
                << ",\"column\":" << result.column
                << ",\"genericName\":" << JsonString(result.genericName)
                << ",\"sourcePath\":" << JsonString(result.sourcePath)
                << ",\"addedModelName\":" << JsonString(result.addedModelName)
                << ",\"status\":" << JsonString(InstanceBuilder::StatusName(result.status))
                << ",\"featureId\":" << result.componentFeatureId
                << ",\"details\":" << JsonString(result.details) << "}";
        }
        out << "]}";
        return out.str();
    }

    void SendState() const {
        if (!webView_) return;
        const std::wstring json = Utf8ToWide(BuildStateJson());
        webView_->PostWebMessageAsString(json.c_str());
    }

    HWND hwnd_ = nullptr;
    HMODULE loaderModule_ = nullptr;
    CreateEnvironmentFn createEnvironment_ = nullptr;
    std::filesystem::path uiIndexPath_;

    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webView_;
    EventRegistrationToken webMessageToken_{};

    bool busy_ = false;
    bool cleanupLoadedModels_ = false;
    WebOperation operation_ = WebOperation::None;
    std::size_t operationIndex_ = 0;
    std::vector<ModelDescriptor> sources_;
    std::unique_ptr<InspectionBuilder> inspectionBuilder_;
    std::unique_ptr<InstanceBuilder> instanceBuilder_;
    std::wstring statusMessage_ = L"Ready";
};
} // namespace

ProError WebToolbox::Show() {
    return WebToolboxHost::Instance().Show();
}

void WebToolbox::Shutdown() {
    WebToolboxHost::Instance().Shutdown();
}
