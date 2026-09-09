#include <ProToolkit.h>
#include <ProMdl.h>
#include <ProObjects.h>
#include <ProUtil.h>
#include <ProView.h>
#include <ProWindows.h>

#include "tools/SimilarCadSearchHost.h"
#include "tools/SimilarCadSearch.h"
#include "common/FolderScanner.h"
#include "common/Logger.h"
#include "common/ModelLoader.h"
#include "common/ModelUtils.h"
#include "common/SessionSnapshot.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr wchar_t kWindowClass[] = L"AventicsSimilarCadHostWindow";
constexpr UINT_PTR kIndexTimer = 0x53434144; // SCAD
constexpr UINT kPipeCommandMessage = WM_APP + 0x61;
constexpr UINT kPipeConnectedMessage = WM_APP + 0x62;
constexpr int kViewCount = 8;

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
        if (c <= 0x7f) bytes.push_back(static_cast<char>(c));
        else bytes += WideToUtf8(std::wstring(1, c));
    }
    return Utf8ToWide(bytes);
}

std::vector<std::wstring> SplitProtocol(const std::wstring& message) {
    std::vector<std::wstring> fields;
    std::size_t start = 0;
    while (start <= message.size()) {
        const std::size_t end = message.find(L'\t', start);
        fields.push_back(UrlDecode(message.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start)));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return fields;
}

bool ParseBool(const std::wstring& value) {
    return value == L"1" || value == L"true" || value == L"True";
}

bool ParsePositiveInt(const std::wstring& value, int& result) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != value.size() || parsed < 1) return false;
        result = parsed;
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
            if (c < 0x20) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec;
            else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

const char* JsonBool(bool value) { return value ? "true" : "false"; }

HMODULE CurrentModule() {
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CurrentModule), &module);
    return module;
}

FolderScanOptions SearchFolderOptions(bool recursive, bool latest) {
    FolderScanOptions options;
    options.includeSubfolders = recursive;
    options.latestCreoVersionOnly = latest;
    options.includeParts = true;
    options.includeAssemblies = false;
    options.includeStep = false;
    return options;
}

struct RotationSpec {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

constexpr std::array<RotationSpec, kViewCount> kViews = {{
    {0.0, 0.0, 0.0},
    {0.0, 180.0, 0.0},
    {0.0, -90.0, 0.0},
    {0.0, 90.0, 0.0},
    {90.0, 0.0, 0.0},
    {-90.0, 0.0, 0.0},
    {-35.264, 45.0, 0.0},
    {-35.264, -45.0, 0.0}
}};

class SimilarCadSearchHostImpl {
public:
    static SimilarCadSearchHostImpl& Instance() {
        static SimilarCadSearchHostImpl host;
        return host;
    }

    ProError Start() {
        if (hwnd_) return PRO_TK_NO_ERROR;

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = CurrentModule();
        windowClass.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            Logger::Error(L"Could not register Similar CAD host window class.");
            return PRO_TK_GENERAL_ERROR;
        }

        hwnd_ = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, CurrentModule(), nullptr);
        if (!hwnd_) {
            Logger::Error(L"Could not create Similar CAD host message window.");
            return PRO_TK_GENERAL_ERROR;
        }

        pipeName_ = L"\\\\.\\pipe\\aventics-similar-cad-" + std::to_wstring(GetCurrentProcessId());
        stopPipe_.store(false);
        pipeThread_ = std::thread([this]() { PipeServerLoop(); });
        Logger::Info(L"Started Similar CAD host at " + pipeName_);
        return PRO_TK_NO_ERROR;
    }

    void Shutdown() {
        if (hwnd_) KillTimer(hwnd_, kIndexTimer);
        busy_ = false;
        sources_.clear();
        pendingRecords_.clear();
        existingRecords_.clear();

        stopPipe_.store(true);
        {
            std::lock_guard<std::mutex> lock(pipeMutex_);
            if (pipeHandle_ != INVALID_HANDLE_VALUE) {
                CancelIoEx(pipeHandle_, nullptr);
                DisconnectNamedPipe(pipeHandle_);
            }
        }
        if (!pipeName_.empty()) {
            HANDLE wake = CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
        }
        if (pipeThread_.joinable()) pipeThread_.join();
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

private:
    SimilarCadSearchHostImpl() = default;
    SimilarCadSearchHostImpl(const SimilarCadSearchHostImpl&) = delete;
    SimilarCadSearchHostImpl& operator=(const SimilarCadSearchHostImpl&) = delete;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        return Instance().HandleWindowMessage(hwnd, message, wParam, lParam);
    }

    LRESULT HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == kPipeCommandMessage) {
            std::unique_ptr<std::wstring> command(reinterpret_cast<std::wstring*>(lParam));
            if (command) HandleProtocol(*command);
            return 0;
        }
        if (message == kPipeConnectedMessage) {
            SendState();
            return 0;
        }
        if (message == WM_TIMER && wParam == kIndexTimer) {
            KillTimer(hwnd, kIndexTimer);
            TickIndex();
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void PipeServerLoop() {
        while (!stopPipe_.load()) {
            HANDLE pipe = CreateNamedPipeW(
                pipeName_.c_str(), PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1, 1024 * 1024, 1024 * 1024, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                Logger::Error(L"Could not create Similar CAD named pipe.");
                return;
            }

            {
                std::lock_guard<std::mutex> lock(pipeMutex_);
                pipeHandle_ = pipe;
            }

            const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                ? TRUE
                : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);
            if (!connected || stopPipe_.load()) {
                {
                    std::lock_guard<std::mutex> lock(pipeMutex_);
                    if (pipeHandle_ == pipe) pipeHandle_ = INVALID_HANDLE_VALUE;
                }
                CloseHandle(pipe);
                if (stopPipe_.load()) return;
                continue;
            }

            clientConnected_.store(true);
            PostMessageW(hwnd_, kPipeConnectedMessage, 0, 0);

            std::string pending;
            char buffer[4096];
            DWORD read = 0;
            while (!stopPipe_.load() && ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
                pending.append(buffer, buffer + read);
                for (;;) {
                    const std::size_t newline = pending.find('\n');
                    if (newline == std::string::npos) break;
                    std::string line = pending.substr(0, newline);
                    pending.erase(0, newline + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) continue;
                    auto* command = new std::wstring(Utf8ToWide(line));
                    if (!PostMessageW(hwnd_, kPipeCommandMessage, 0, reinterpret_cast<LPARAM>(command))) delete command;
                }
            }

            clientConnected_.store(false);
            {
                std::lock_guard<std::mutex> lock(pipeMutex_);
                if (pipeHandle_ == pipe) pipeHandle_ = INVALID_HANDLE_VALUE;
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }

    bool SendLine(const std::string& message) {
        std::lock_guard<std::mutex> lock(pipeMutex_);
        if (!clientConnected_.load() || pipeHandle_ == INVALID_HANDLE_VALUE) return false;
        const std::string line = message + "\n";
        DWORD written = 0;
        return WriteFile(pipeHandle_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) && written == line.size();
    }

    void HandleProtocol(const std::wstring& raw) {
        const auto fields = SplitProtocol(raw);
        if (fields.empty()) return;
        const auto& action = fields[0];

        if (action == L"ready" || action == L"refresh") {
            SendState();
            return;
        }
        if (action == L"cancel") {
            if (busy_) FinishIndex(true);
            return;
        }
        if (action == L"index" && fields.size() >= 4) {
            StartIndex(fields[1], ParseBool(fields[2]), ParseBool(fields[3]));
            return;
        }
        if (action == L"search" && fields.size() >= 4) {
            int topK = 20;
            if (!ParsePositiveInt(fields[3], topK)) topK = 20;
            Search(fields[1], fields[2], topK);
            return;
        }
        if (action == L"open" && fields.size() >= 2) {
            OpenModel(fields[1]);
            return;
        }

        statusMessage_ = L"Unknown Similar CAD action: " + action;
        SendState();
    }

    void StartIndex(const std::wstring& folder, bool recursive, bool latest) {
        if (busy_) return;
        if (folder.empty()) {
            statusMessage_ = L"Choose a Creo part library folder first.";
            SendState();
            return;
        }

        libraryFolder_ = folder;
        recursive_ = recursive;
        latest_ = latest;
        queryImage_.clear();
        results_.clear();
        cacheDirectory_ = SimilarCadSearchEngine::CacheDirectoryForLibrary(libraryFolder_);

        const auto scan = FolderScanner::Discover(libraryFolder_, SearchFolderOptions(recursive_, latest_));
        if (!scan.error.empty() || scan.models.empty()) {
            statusMessage_ = scan.error.empty() ? L"No Creo part files were found in this library." : scan.error;
            SendState();
            return;
        }

        std::wstring indexedLibrary;
        std::vector<SimilarCadModelRecord> previous;
        std::wstring loadError;
        SimilarCadSearchEngine::LoadIndex(cacheDirectory_, indexedLibrary, previous, loadError);
        existingRecords_.clear();
        for (auto& record : previous) {
            existingRecords_.emplace(SimilarCadSearchEngine::StablePathKey(record.sourcePath), std::move(record));
        }

        sources_ = scan.models;
        pendingRecords_.clear();
        pendingRecords_.reserve(sources_.size());
        operationIndex_ = 0;
        indexedCount_ = 0;
        skippedCount_ = 0;
        failedCount_ = 0;
        busy_ = true;
        statusMessage_ = L"Preparing Similar CAD index...";
        SendState();
        ScheduleIndexTick();
    }

    void ScheduleIndexTick() {
        if (busy_ && hwnd_) SetTimer(hwnd_, kIndexTimer, 1, nullptr);
    }

    bool ExistingRecordUsable(const ModelDescriptor& source, const SimilarCadModelRecord& record) const {
        if (record.modifiedStamp != SimilarCadSearchEngine::FileStamp(source.sourcePath)) return false;
        if (record.views.size() != kViewCount) return false;
        for (const auto& view : record.views) {
            if (view.signature.size() != SimilarCadSearchEngine::SignatureLength || !fs::exists(fs::path(view.renderPath))) return false;
        }
        return true;
    }

    void TickIndex() {
        if (!busy_) return;
        if (operationIndex_ >= sources_.size()) {
            FinishIndex(false);
            return;
        }

        ModelDescriptor source = sources_[operationIndex_];
        statusMessage_ = L"Indexing " + source.displayName;
        const std::wstring key = SimilarCadSearchEngine::StablePathKey(source.sourcePath);
        const auto existing = existingRecords_.find(key);
        if (existing != existingRecords_.end() && ExistingRecordUsable(source, existing->second)) {
            pendingRecords_.push_back(existing->second);
            ++skippedCount_;
        } else {
            SimilarCadModelRecord record;
            std::wstring error;
            if (BuildRecord(source, record, error)) {
                pendingRecords_.push_back(std::move(record));
                ++indexedCount_;
            } else {
                ++failedCount_;
                Logger::Error(L"Similar CAD indexing failed for " + source.sourcePath + L": " + error);
            }
        }

        ++operationIndex_;
        statusMessage_ = L"Similar CAD index | " + std::to_wstring(operationIndex_) + L" / " + std::to_wstring(sources_.size());
        SendState();
        if (operationIndex_ >= sources_.size()) FinishIndex(false);
        else ScheduleIndexTick();
    }

    bool BuildRecord(ModelDescriptor source, SimilarCadModelRecord& record, std::wstring& error) {
        SessionSnapshot before = SessionSnapshot::Capture();
        const ProError loadError = ModelLoader::Load(source, before);
        if (loadError != PRO_TK_NO_ERROR || !source.model) {
            error = L"Load failed: " + ModelUtils::ErrorName(loadError);
            return false;
        }

        record.modelName = source.displayName;
        record.sourcePath = source.sourcePath;
        record.creoFileVersion = source.creoFileVersion;
        record.modifiedStamp = SimilarCadSearchEngine::FileStamp(source.sourcePath);

        const bool rendered = RenderViews(source.model, source.sourcePath, record.views, error);
        before.CleanupNewModels();
        return rendered;
    }

    bool RenderViews(ProMdl model, const std::wstring& sourcePath, std::vector<SimilarCadViewRecord>& views, std::wstring& error) {
        views.clear();
        if (!model) {
            error = L"No model was loaded for rendering.";
            return false;
        }

        std::error_code ec;
        const fs::path rendersDir = cacheDirectory_ / L"renders";
        fs::create_directories(rendersDir, ec);
        if (ec) {
            error = L"Could not create render cache directory.";
            return false;
        }

        int previousWindow = -1;
        const bool havePreviousWindow = ProWindowCurrentGet(&previousWindow) == PRO_TK_NO_ERROR;

        int renderWindow = -1;
        const bool existingWindow = ProMdlWindowGet(model, &renderWindow) == PRO_TK_NO_ERROR;
        bool createdWindow = false;
        if (!existingWindow) {
            ProMdlName name;
            name[0] = L'\0';
            const ProError nameError = ProMdlMdlnameGet(model, name);
            if (nameError != PRO_TK_NO_ERROR) {
                error = L"Could not read model name for rendering: " + ModelUtils::ErrorName(nameError);
                return false;
            }
            const ProError windowError = ProObjectwindowMdlnameCreate(name, PRO_PART, &renderWindow);
            if (windowError != PRO_TK_NO_ERROR) {
                error = L"Could not create Creo render window: " + ModelUtils::ErrorName(windowError);
                return false;
            }
            createdWindow = true;
        }

        if (ProWindowCurrentSet(renderWindow) != PRO_TK_NO_ERROR) {
            error = L"Could not make the render window current.";
            if (createdWindow && havePreviousWindow) ProWindowCurrentSet(previousWindow);
            if (createdWindow) ProWindowDelete(renderWindow);
            return false;
        }

        ProMatrix originalMatrix{};
        const bool haveOriginalMatrix = existingWindow && ProViewMatrixGet(model, nullptr, originalMatrix) == PRO_TK_NO_ERROR;
        ProMdlDisplay(model);

        bool success = true;
        for (int i = 0; i < kViewCount; ++i) {
            const auto& rotation = kViews[static_cast<std::size_t>(i)];
            ProError viewError = ProViewReset(model, nullptr);
            if (viewError == PRO_TK_NO_ERROR && rotation.x != 0.0) viewError = ProViewRotate(model, nullptr, PRO_X_ROTATION, rotation.x);
            if (viewError == PRO_TK_NO_ERROR && rotation.y != 0.0) viewError = ProViewRotate(model, nullptr, PRO_Y_ROTATION, rotation.y);
            if (viewError == PRO_TK_NO_ERROR && rotation.z != 0.0) viewError = ProViewRotate(model, nullptr, PRO_Z_ROTATION, rotation.z);
            if (viewError == PRO_TK_NO_ERROR) viewError = ProViewRefit(model, nullptr);
            if (viewError != PRO_TK_NO_ERROR) {
                error = L"Could not orient Creo view " + std::to_wstring(i + 1) + L": " + ModelUtils::ErrorName(viewError);
                success = false;
                break;
            }

            ProWindowRepaint(renderWindow);
            ProWindowDeviceFlush(renderWindow);

            const std::wstring fileName = SimilarCadSearchEngine::StablePathKey(sourcePath) + L"_v" + std::to_wstring(i) + L".jpg";
            const fs::path renderPath = rendersDir / fileName;
            ProPath outputPath;
            if (!ModelUtils::CopyToProPath(renderPath.wstring(), outputPath)) {
                error = L"Render cache path is too long for Creo TOOLKIT.";
                success = false;
                break;
            }

            const ProError rasterError = ProRasterFileWrite(
                renderWindow, PRORASTERDEPTH_24, 4.0, 4.0,
                PRORASTERDPI_100, PRORASTERTYPE_JPEG, outputPath);
            if (rasterError != PRO_TK_NO_ERROR) {
                error = L"Creo raster export failed: " + ModelUtils::ErrorName(rasterError);
                success = false;
                break;
            }

            SimilarCadViewRecord view;
            view.renderPath = renderPath.wstring();
            if (!SimilarCadSearchEngine::ComputeSignature(view.renderPath, view.signature, error)) {
                success = false;
                break;
            }
            views.push_back(std::move(view));
        }

        if (haveOriginalMatrix) {
            ProViewMatrixSet(model, nullptr, originalMatrix);
            ProWindowRepaint(renderWindow);
        }
        if (havePreviousWindow && previousWindow >= 0 && previousWindow != renderWindow) ProWindowCurrentSet(previousWindow);
        if (createdWindow) ProWindowDelete(renderWindow);

        if (!success) views.clear();
        return success;
    }

    void FinishIndex(bool cancelled) {
        if (!busy_) return;
        if (hwnd_) KillTimer(hwnd_, kIndexTimer);
        busy_ = false;

        if (cancelled) {
            statusMessage_ = L"Similar CAD indexing cancelled. The previous saved index was kept.";
            sources_.clear();
            pendingRecords_.clear();
            existingRecords_.clear();
            operationIndex_ = 0;
            SendState();
            return;
        }

        std::wstring error;
        if (!SimilarCadSearchEngine::SaveIndex(cacheDirectory_, libraryFolder_, pendingRecords_, error)) {
            statusMessage_ = error;
        } else {
            activeRecords_ = pendingRecords_;
            statusMessage_ = L"Similar CAD index ready: " + std::to_wstring(activeRecords_.size()) + L" parts.";
        }
        sources_.clear();
        pendingRecords_.clear();
        existingRecords_.clear();
        operationIndex_ = 0;
        SendState();
    }

    void Search(const std::wstring& folder, const std::wstring& queryImage, int topK) {
        if (busy_) return;
        if (folder.empty() || queryImage.empty()) {
            statusMessage_ = L"Choose both a CAD library and a query image before searching.";
            SendState();
            return;
        }

        libraryFolder_ = folder;
        queryImage_ = queryImage;
        topK_ = topK;
        cacheDirectory_ = SimilarCadSearchEngine::CacheDirectoryForLibrary(libraryFolder_);

        std::wstring indexedLibrary;
        std::wstring error;
        if (!SimilarCadSearchEngine::LoadIndex(cacheDirectory_, indexedLibrary, activeRecords_, error)) {
            statusMessage_ = error;
            results_.clear();
            SendState();
            return;
        }

        if (!SimilarCadSearchEngine::Search(queryImage_, activeRecords_, static_cast<std::size_t>(topK_), results_, error)) {
            statusMessage_ = error;
            results_.clear();
            SendState();
            return;
        }

        statusMessage_ = L"Found " + std::to_wstring(results_.size()) + L" visually similar Creo parts.";
        SendState();
    }

    void OpenModel(const std::wstring& path) {
        if (busy_ || path.empty()) return;
        ProMdl model = nullptr;
        const ProError loadError = ModelLoader::LoadPath(path, PRO_MDL_PART, &model);
        if (loadError != PRO_TK_NO_ERROR || !model) {
            statusMessage_ = L"Could not open result: " + ModelUtils::ErrorName(loadError);
            SendState();
            return;
        }
        const ProError displayError = ModelUtils::DisplayModel(model);
        statusMessage_ = displayError == PRO_TK_NO_ERROR
            ? L"Opened result in Creo: " + ModelUtils::ModelName(model)
            : L"Result loaded but could not be displayed: " + ModelUtils::ErrorName(displayError);
        SendState();
    }

    std::string BuildStateJson() const {
        std::size_t viewCount = 0;
        for (const auto& record : activeRecords_) viewCount += record.views.size();

        std::ostringstream out;
        out << "{\"type\":\"similarState\",\"protocolVersion\":1";
        out << ",\"busy\":" << JsonBool(busy_);
        out << ",\"progress\":{\"done\":" << operationIndex_
            << ",\"total\":" << sources_.size()
            << ",\"indexed\":" << indexedCount_
            << ",\"skipped\":" << skippedCount_
            << ",\"failed\":" << failedCount_
            << ",\"message\":" << JsonString(statusMessage_) << "}";
        out << ",\"settings\":{\"folder\":" << JsonString(libraryFolder_)
            << ",\"queryImage\":" << JsonString(queryImage_)
            << ",\"recursive\":" << JsonBool(recursive_)
            << ",\"latest\":" << JsonBool(latest_)
            << ",\"topK\":" << topK_ << "}";
        out << ",\"index\":{\"models\":" << activeRecords_.size()
            << ",\"views\":" << viewCount
            << ",\"cachePath\":" << JsonString(cacheDirectory_.wstring())
            << ",\"engine\":\"prototype-signature-v1\"}";
        out << ",\"results\":[";
        for (std::size_t i = 0; i < results_.size(); ++i) {
            if (i) out << ',';
            const auto& result = results_[i];
            out << "{\"modelName\":" << JsonString(result.modelName)
                << ",\"sourcePath\":" << JsonString(result.sourcePath)
                << ",\"previewPath\":" << JsonString(result.previewPath)
                << ",\"score\":" << std::fixed << std::setprecision(2) << result.score << "}";
        }
        out << "]}";
        return out.str();
    }

    void SendState() {
        SendLine(BuildStateJson());
    }

    HWND hwnd_ = nullptr;
    std::wstring pipeName_;
    std::thread pipeThread_;
    std::atomic<bool> stopPipe_{false};
    std::atomic<bool> clientConnected_{false};
    HANDLE pipeHandle_ = INVALID_HANDLE_VALUE;
    std::mutex pipeMutex_;

    bool busy_ = false;
    bool recursive_ = false;
    bool latest_ = true;
    int topK_ = 20;
    std::size_t operationIndex_ = 0;
    std::size_t indexedCount_ = 0;
    std::size_t skippedCount_ = 0;
    std::size_t failedCount_ = 0;
    std::wstring libraryFolder_;
    std::wstring queryImage_;
    fs::path cacheDirectory_;
    std::wstring statusMessage_ = L"Similar CAD Search ready.";

    std::vector<ModelDescriptor> sources_;
    std::unordered_map<std::wstring, SimilarCadModelRecord> existingRecords_;
    std::vector<SimilarCadModelRecord> pendingRecords_;
    std::vector<SimilarCadModelRecord> activeRecords_;
    std::vector<SimilarCadSearchResult> results_;
};
} // namespace

ProError SimilarCadSearchHost::Start() {
    return SimilarCadSearchHostImpl::Instance().Start();
}

void SimilarCadSearchHost::Shutdown() {
    SimilarCadSearchHostImpl::Instance().Shutdown();
}
