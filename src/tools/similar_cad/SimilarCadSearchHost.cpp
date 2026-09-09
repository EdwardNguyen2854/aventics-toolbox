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
#include <cmath>
#include <condition_variable>
#include <cstring>
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
constexpr UINT kIndexTickDelayMs = 45;
constexpr std::size_t kViewCount = 8;
constexpr int kDefaultLibraryPageSize = 24;

struct Vec3 {
    double x;
    double y;
    double z;
};

struct ViewSpec {
    const wchar_t* name;
    Vec3 forward;
    Vec3 up;
};

constexpr std::array<ViewSpec, kViewCount> kViews = {{
    {L"FRONT",  { 0.0,  0.0,  1.0}, {0.0, 1.0,  0.0}},
    {L"BACK",   { 0.0,  0.0, -1.0}, {0.0, 1.0,  0.0}},
    {L"RIGHT",  { 1.0,  0.0,  0.0}, {0.0, 1.0,  0.0}},
    {L"LEFT",   {-1.0,  0.0,  0.0}, {0.0, 1.0,  0.0}},
    {L"TOP",    { 0.0,  1.0,  0.0}, {0.0, 0.0, -1.0}},
    {L"BOTTOM", { 0.0, -1.0,  0.0}, {0.0, 0.0,  1.0}},
    {L"ISO_NE", { 1.0, -1.0,  1.0}, {0.0, 1.0,  0.0}},
    {L"ISO_NW", {-1.0, -1.0,  1.0}, {0.0, 1.0,  0.0}}
}};

enum class IndexStep {
    SelectSource,
    LoadSource,
    OrientView,
    CaptureView,
    AnalyzeView,
    FinalizeSource
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

Vec3 Normalize(Vec3 value) {
    const double length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 1e-12) return {0.0, 0.0, 1.0};
    return {value.x / length, value.y / length, value.z / length};
}

Vec3 Cross(const Vec3& left, const Vec3& right) {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x
    };
}

void BuildViewMatrix(const ViewSpec& spec, ProMatrix matrix) {
    const Vec3 forward = Normalize(spec.forward);
    Vec3 right = Normalize(Cross(spec.up, forward));
    if (std::abs(right.x) + std::abs(right.y) + std::abs(right.z) < 1e-9)
        right = {1.0, 0.0, 0.0};
    const Vec3 up = Normalize(Cross(forward, right));

    std::memset(matrix, 0, sizeof(ProMatrix));
    matrix[0][0] = right.x;
    matrix[0][1] = right.y;
    matrix[0][2] = right.z;
    matrix[1][0] = up.x;
    matrix[1][1] = up.y;
    matrix[1][2] = up.z;
    matrix[2][0] = forward.x;
    matrix[2][1] = forward.y;
    matrix[2][2] = forward.z;
    matrix[3][3] = 1.0;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

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
        writerThread_ = std::thread([this]() { WriterLoop(); });
        Logger::Info(L"Started Similar CAD host at " + pipeName_);
        return PRO_TK_NO_ERROR;
    }

    void Shutdown() {
        if (hwnd_) KillTimer(hwnd_, kIndexTimer);
        busy_ = false;
        CleanupCapture();
        sources_.clear();
        pendingRecords_.clear();
        existingRecords_.clear();

        stopPipe_.store(true);
        outboundCv_.notify_all();

        HANDLE pipe = INVALID_HANDLE_VALUE;
        {
            std::lock_guard<std::mutex> lock(pipeMutex_);
            pipe = pipeHandle_;
        }
        if (pipe != INVALID_HANDLE_VALUE) CancelIoEx(pipe, nullptr);
        if (!pipeName_.empty()) {
            HANDLE wake = CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
        }

        if (pipeThread_.joinable()) pipeThread_.join();
        if (writerThread_.joinable()) writerThread_.join();
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

private:
    struct CaptureState {
        ModelDescriptor source;
        std::unique_ptr<SessionSnapshot> before;
        SimilarCadModelRecord record;
        std::wstring error;
        std::wstring currentRenderPath;
        std::size_t viewIndex = 0;
        int previousWindow = -1;
        int renderWindow = -1;
        bool havePreviousWindow = false;
        bool modelAlreadyDisplayed = false;
        bool createdWindow = false;
        bool preserveView = false;
        ProMatrix originalMatrix{};
    };

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
                    std::lock_guard<std::mutex> writeLock(writeMutex_);
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
                std::lock_guard<std::mutex> writeLock(writeMutex_);
                std::lock_guard<std::mutex> lock(pipeMutex_);
                if (pipeHandle_ == pipe) pipeHandle_ = INVALID_HANDLE_VALUE;
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
            }
        }
    }

    void WriterLoop() {
        while (!stopPipe_.load()) {
            std::string message;
            {
                std::unique_lock<std::mutex> lock(outboundMutex_);
                outboundCv_.wait(lock, [this]() { return stopPipe_.load() || outboundPending_; });
                if (stopPipe_.load()) return;
                message = std::move(outboundLatest_);
                outboundLatest_.clear();
                outboundPending_ = false;
            }
            if (!clientConnected_.load() || message.empty()) continue;

            std::lock_guard<std::mutex> writeLock(writeMutex_);
            HANDLE pipe = INVALID_HANDLE_VALUE;
            {
                std::lock_guard<std::mutex> lock(pipeMutex_);
                pipe = pipeHandle_;
            }
            if (pipe == INVALID_HANDLE_VALUE || !clientConnected_.load()) continue;
            const std::string line = message + "\n";
            DWORD written = 0;
            WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        }
    }

    void QueueLine(std::string message) {
        if (!clientConnected_.load()) return;
        {
            std::lock_guard<std::mutex> lock(outboundMutex_);
            outboundLatest_ = std::move(message);
            outboundPending_ = true;
        }
        outboundCv_.notify_one();
    }

    void HandleProtocol(const std::wstring& raw) {
        const auto fields = SplitProtocol(raw);
        if (fields.empty()) return;
        const std::wstring& action = fields[0];

        if (action == L"ready" || action == L"refresh") { SendState(); return; }
        if (action == L"cancel") { if (busy_) FinishIndex(true); return; }
        if (action == L"index" && fields.size() >= 4) {
            StartIndex(fields[1], ParseBool(fields[2]), ParseBool(fields[3]));
            return;
        }
        if (action == L"prepareQuery" && fields.size() >= 3) {
            PrepareQuery(fields[1], ParseBool(fields[2]));
            return;
        }
        if (action == L"search" && fields.size() >= 5) {
            int topK = 20;
            if (!ParsePositiveInt(fields[3], topK)) topK = 20;
            Search(fields[1], fields[2], topK, ParseBool(fields[4]));
            return;
        }
        if (action == L"browse" && fields.size() >= 5) {
            int page = 1;
            int pageSize = kDefaultLibraryPageSize;
            ParsePositiveInt(fields[3], page);
            ParsePositiveInt(fields[4], pageSize);
            BrowseLibrary(fields[1], fields[2], page - 1, std::clamp(pageSize, 8, 100));
            return;
        }
        if (action == L"inspect" && fields.size() >= 2) {
            InspectLibraryItem(fields[1]);
            return;
        }
        if (action == L"open" && fields.size() >= 2) { OpenModel(fields[1]); return; }

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
        std::wstring ignoredLoadError;
        if (SimilarCadSearchEngine::LoadIndex(cacheDirectory_, indexedLibrary, previous, ignoredLoadError)) {
            activeRecords_ = previous;
        } else {
            activeRecords_.clear();
        }
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
        indexStep_ = IndexStep::SelectSource;
        capture_ = {};
        busy_ = true;
        statusMessage_ = L"Preparing Similar CAD index...";
        SendState();
        ScheduleIndexTick();
    }

    void ScheduleIndexTick() {
        if (busy_ && hwnd_) SetTimer(hwnd_, kIndexTimer, kIndexTickDelayMs, nullptr);
    }

    bool ExistingRecordUsable(const ModelDescriptor& source, const SimilarCadModelRecord& record) const {
        if (record.modifiedStamp != SimilarCadSearchEngine::FileStamp(source.sourcePath)) return false;
        if (record.views.size() != kViewCount) return false;
        for (std::size_t i = 0; i < record.views.size(); ++i) {
            const auto& view = record.views[i];
            if (view.viewName != kViews[i].name) return false;
            if (view.descriptor.signature.size() != SimilarCadSearchEngine::SignatureLength) return false;
            if (!fs::exists(fs::path(view.renderPath))) return false;
        }
        return true;
    }

    void TickIndex() {
        if (!busy_) return;
        try {
            switch (indexStep_) {
            case IndexStep::SelectSource: SelectSourceStep(); break;
            case IndexStep::LoadSource: LoadSourceStep(); break;
            case IndexStep::OrientView: OrientViewStep(); break;
            case IndexStep::CaptureView: CaptureViewStep(); break;
            case IndexStep::AnalyzeView: AnalyzeViewStep(); break;
            case IndexStep::FinalizeSource: FinalizeSourceStep(); break;
            }
        } catch (...) {
            capture_.error = L"Unhandled exception while indexing this model.";
            Logger::Error(L"Unhandled exception inside Similar CAD index step.");
            indexStep_ = IndexStep::FinalizeSource;
        }

        if (busy_) {
            SendState();
            ScheduleIndexTick();
        }
    }

    void SelectSourceStep() {
        if (operationIndex_ >= sources_.size()) {
            FinishIndex(false);
            return;
        }

        const ModelDescriptor& source = sources_[operationIndex_];
        const std::wstring key = SimilarCadSearchEngine::StablePathKey(source.sourcePath);
        const auto existing = existingRecords_.find(key);
        if (existing != existingRecords_.end() && ExistingRecordUsable(source, existing->second)) {
            pendingRecords_.push_back(existing->second);
            ++skippedCount_;
            ++operationIndex_;
            statusMessage_ = L"Unchanged: " + source.displayName;
            return;
        }

        capture_ = {};
        capture_.source = source;
        statusMessage_ = L"Loading " + source.displayName;
        indexStep_ = IndexStep::LoadSource;
    }

    void LoadSourceStep() {
        capture_.before = std::make_unique<SessionSnapshot>(SessionSnapshot::Capture());
        const ProError loadError = ModelLoader::Load(capture_.source, *capture_.before);
        if (loadError != PRO_TK_NO_ERROR || !capture_.source.model) {
            capture_.error = L"Load failed: " + ModelUtils::ErrorName(loadError);
            indexStep_ = IndexStep::FinalizeSource;
            return;
        }

        capture_.record.modelName = capture_.source.displayName;
        capture_.record.sourcePath = capture_.source.sourcePath;
        capture_.record.creoFileVersion = capture_.source.creoFileVersion;
        capture_.record.modifiedStamp = SimilarCadSearchEngine::FileStamp(capture_.source.sourcePath);
        capture_.record.views.clear();
        capture_.record.views.reserve(kViewCount);

        capture_.havePreviousWindow = ProWindowCurrentGet(&capture_.previousWindow) == PRO_TK_NO_ERROR;
        capture_.modelAlreadyDisplayed = ProMdlWindowGet(capture_.source.model, &capture_.renderWindow) == PRO_TK_NO_ERROR;
        capture_.createdWindow = !capture_.modelAlreadyDisplayed;
        if (capture_.createdWindow) {
            ProMdlName name;
            name[0] = L'\0';
            const ProError nameError = ProMdlMdlnameGet(capture_.source.model, name);
            if (nameError != PRO_TK_NO_ERROR) {
                capture_.error = L"Could not read model name for rendering: " + ModelUtils::ErrorName(nameError);
                indexStep_ = IndexStep::FinalizeSource;
                return;
            }
            const ProError windowError = ProObjectwindowMdlnameCreate(name, PRO_PART, &capture_.renderWindow);
            if (windowError != PRO_TK_NO_ERROR) {
                capture_.error = L"Could not create Creo render window: " + ModelUtils::ErrorName(windowError);
                indexStep_ = IndexStep::FinalizeSource;
                return;
            }
        }

        if (ProWindowCurrentSet(capture_.renderWindow) != PRO_TK_NO_ERROR) {
            capture_.error = L"Could not make the Creo render window current.";
            indexStep_ = IndexStep::FinalizeSource;
            return;
        }
        capture_.preserveView = capture_.modelAlreadyDisplayed &&
            ProViewMatrixGet(capture_.source.model, nullptr, capture_.originalMatrix) == PRO_TK_NO_ERROR;
        ProMdlDisplay(capture_.source.model);
        capture_.viewIndex = 0;
        statusMessage_ = L"Preparing canonical views for " + capture_.source.displayName;
        indexStep_ = IndexStep::OrientView;
    }

    void OrientViewStep() {
        if (!capture_.source.model || capture_.viewIndex >= kViews.size()) {
            indexStep_ = IndexStep::FinalizeSource;
            return;
        }

        ProMatrix matrix{};
        BuildViewMatrix(kViews[capture_.viewIndex], matrix);
        ProError error = ProViewMatrixSet(capture_.source.model, nullptr, matrix);
        if (error == PRO_TK_NO_ERROR) error = ProViewRefit(capture_.source.model, nullptr);
        if (error != PRO_TK_NO_ERROR) {
            capture_.error = L"Could not orient canonical view " + std::wstring(kViews[capture_.viewIndex].name) +
                             L": " + ModelUtils::ErrorName(error);
            indexStep_ = IndexStep::FinalizeSource;
            return;
        }

        ProWindowRepaint(capture_.renderWindow);
        statusMessage_ = L"View " + std::to_wstring(capture_.viewIndex + 1) + L" / " +
                         std::to_wstring(kViewCount) + L" | " + kViews[capture_.viewIndex].name +
                         L" | " + capture_.source.displayName;
        indexStep_ = IndexStep::CaptureView;
    }

    void CaptureViewStep() {
        std::error_code ec;
        const fs::path rendersDir = cacheDirectory_ / L"renders";
        fs::create_directories(rendersDir, ec);
        if (ec) {
            capture_.error = L"Could not create render cache directory.";
            indexStep_ = IndexStep::FinalizeSource;
            return;
        }

        const std::wstring fileName = SimilarCadSearchEngine::StablePathKey(capture_.source.sourcePath) +
            L"_" + kViews[capture_.viewIndex].name + L".jpg";
        const fs::path renderPath = rendersDir / fileName;
        ProPath outputPath;
        if (!ModelUtils::CopyToProPath(renderPath.wstring(), outputPath)) {
            capture_.error = L"Render cache path is too long for Creo TOOLKIT.";
            indexStep_ = IndexStep::FinalizeSource;
            return;
        }

        const ProError rasterError = ProRasterFileWrite(
            capture_.renderWindow, PRORASTERDEPTH_24, 4.0, 4.0,
            PRORASTERDPI_100, PRORASTERTYPE_JPEG, outputPath);
        if (rasterError != PRO_TK_NO_ERROR) {
            capture_.error = L"Creo raster export failed for " + std::wstring(kViews[capture_.viewIndex].name) +
                             L": " + ModelUtils::ErrorName(rasterError);
            indexStep_ = IndexStep::FinalizeSource;
            return;
        }
        capture_.currentRenderPath = renderPath.wstring();
        indexStep_ = IndexStep::AnalyzeView;
    }

    void AnalyzeViewStep() {
        SimilarCadViewRecord view;
        view.viewName = kViews[capture_.viewIndex].name;
        view.renderPath = capture_.currentRenderPath;
        if (!SimilarCadSearchEngine::ComputeDescriptor(
                view.renderPath, true, view.descriptor, capture_.error, nullptr)) {
            indexStep_ = IndexStep::FinalizeSource;
            return;
        }
        capture_.record.views.push_back(std::move(view));
        capture_.currentRenderPath.clear();
        ++capture_.viewIndex;
        indexStep_ = capture_.viewIndex < kViews.size()
            ? IndexStep::OrientView
            : IndexStep::FinalizeSource;
    }

    void FinalizeSourceStep() {
        const bool success = capture_.error.empty() && capture_.record.views.size() == kViewCount;
        const std::wstring sourcePath = capture_.source.sourcePath;
        const std::wstring displayName = capture_.source.displayName;
        const std::wstring error = capture_.error;
        SimilarCadModelRecord completed;
        if (success) completed = std::move(capture_.record);
        CleanupCapture();

        if (success) {
            pendingRecords_.push_back(std::move(completed));
            ++indexedCount_;
            statusMessage_ = L"Captured " + displayName;
        } else {
            ++failedCount_;
            statusMessage_ = L"Failed: " + displayName;
            Logger::Error(L"Similar CAD indexing failed for " + sourcePath + L": " +
                          (error.empty() ? L"Incomplete view capture." : error));
        }
        ++operationIndex_;
        indexStep_ = IndexStep::SelectSource;
    }

    void CleanupCapture() {
        if (capture_.source.model) {
            if (capture_.preserveView) {
                ProViewMatrixSet(capture_.source.model, nullptr, capture_.originalMatrix);
                if (capture_.renderWindow >= 0) ProWindowRepaint(capture_.renderWindow);
            }
            const bool canLeaveRenderWindow = capture_.havePreviousWindow && capture_.previousWindow >= 0 &&
                                              capture_.previousWindow != capture_.renderWindow;
            if (canLeaveRenderWindow) ProWindowCurrentSet(capture_.previousWindow);
            if (capture_.createdWindow && canLeaveRenderWindow && capture_.renderWindow >= 0)
                ProWindowDelete(capture_.renderWindow);
        }
        if (capture_.before) capture_.before->CleanupNewModels();
        capture_ = {};
    }

    void FinishIndex(bool cancelled) {
        if (!busy_) return;
        if (hwnd_) KillTimer(hwnd_, kIndexTimer);
        CleanupCapture();
        busy_ = false;

        if (cancelled) {
            statusMessage_ = L"Similar CAD indexing cancelled. The previous saved index was kept.";
        } else {
            std::wstring error;
            if (!SimilarCadSearchEngine::SaveIndex(cacheDirectory_, libraryFolder_, pendingRecords_, error)) {
                statusMessage_ = error;
            } else {
                activeRecords_ = pendingRecords_;
                statusMessage_ = L"Similar CAD index ready: " + std::to_wstring(activeRecords_.size()) + L" parts.";
                RefreshLibraryMatches();
            }
        }

        sources_.clear();
        pendingRecords_.clear();
        existingRecords_.clear();
        operationIndex_ = 0;
        indexStep_ = IndexStep::SelectSource;
        SendState();
    }

    bool EnsureIndexLoaded(const std::wstring& folder, std::wstring& error) {
        if (folder.empty()) {
            error = L"Choose an indexed CAD library first.";
            return false;
        }
        const fs::path requestedCache = SimilarCadSearchEngine::CacheDirectoryForLibrary(folder);
        if (folder == libraryFolder_ && requestedCache == cacheDirectory_ && !activeRecords_.empty()) return true;

        std::wstring indexedLibrary;
        std::vector<SimilarCadModelRecord> loaded;
        if (!SimilarCadSearchEngine::LoadIndex(requestedCache, indexedLibrary, loaded, error)) return false;
        libraryFolder_ = folder;
        cacheDirectory_ = requestedCache;
        activeRecords_ = std::move(loaded);
        return true;
    }

    void PrepareQuery(const std::wstring& imagePath, bool autoCrop) {
        if (busy_) return;
        std::wstring error;
        SimilarCadQueryDescriptor descriptor;
        if (!SimilarCadSearchEngine::PrepareQuery(imagePath, autoCrop, descriptor, error)) {
            queryReady_ = false;
            queryDescriptor_ = {};
            statusMessage_ = error;
            SendState();
            return;
        }
        queryDescriptor_ = std::move(descriptor);
        queryImage_ = imagePath;
        queryAutoCrop_ = autoCrop;
        queryReady_ = true;
        statusMessage_ = L"Query normalized. Review the processed preview before searching.";
        SendState();
    }

    void Search(const std::wstring& folder, const std::wstring& queryImage, int topK, bool autoCrop) {
        if (busy_) return;
        if (folder.empty() || queryImage.empty()) {
            statusMessage_ = L"Choose both a CAD library and a query image before searching.";
            SendState();
            return;
        }

        std::wstring error;
        if (!EnsureIndexLoaded(folder, error)) {
            results_.clear();
            statusMessage_ = error;
            SendState();
            return;
        }

        if (!queryReady_ || queryDescriptor_.sourcePath != queryImage || queryDescriptor_.autoCrop != autoCrop) {
            SimilarCadQueryDescriptor descriptor;
            if (!SimilarCadSearchEngine::PrepareQuery(queryImage, autoCrop, descriptor, error)) {
                results_.clear();
                queryReady_ = false;
                statusMessage_ = error;
                SendState();
                return;
            }
            queryDescriptor_ = std::move(descriptor);
            queryReady_ = true;
        }

        queryImage_ = queryImage;
        queryAutoCrop_ = autoCrop;
        topK_ = topK;
        if (!SimilarCadSearchEngine::Search(queryDescriptor_, activeRecords_, static_cast<std::size_t>(topK_), results_, error)) {
            results_.clear();
            statusMessage_ = error;
            SendState();
            return;
        }

        statusMessage_ = L"Found " + std::to_wstring(results_.size()) + L" similar Creo parts using hybrid shape ranking.";
        SendState();
    }

    void BrowseLibrary(const std::wstring& folder, const std::wstring& filter, int page, int pageSize) {
        std::wstring error;
        if (!EnsureIndexLoaded(folder, error)) {
            libraryMatches_.clear();
            statusMessage_ = error;
            SendState();
            return;
        }
        libraryRequested_ = true;
        libraryFilter_ = filter;
        libraryPage_ = std::max(0, page);
        libraryPageSize_ = std::clamp(pageSize, 8, 100);
        RefreshLibraryMatches();
        statusMessage_ = L"Library: " + std::to_wstring(libraryMatches_.size()) + L" matching indexed parts.";
        SendState();
    }

    void RefreshLibraryMatches() {
        if (!libraryRequested_) return;
        libraryMatches_.clear();
        const std::wstring filter = Lower(libraryFilter_);
        for (std::size_t i = 0; i < activeRecords_.size(); ++i) {
            const auto& record = activeRecords_[i];
            if (filter.empty()) {
                libraryMatches_.push_back(i);
                continue;
            }
            const std::wstring haystack = Lower(record.modelName + L" " + record.sourcePath);
            if (haystack.find(filter) != std::wstring::npos) libraryMatches_.push_back(i);
        }
        const int pageCount = std::max(1, static_cast<int>((libraryMatches_.size() + libraryPageSize_ - 1) /
                                                           static_cast<std::size_t>(libraryPageSize_)));
        libraryPage_ = std::clamp(libraryPage_, 0, pageCount - 1);
    }

    void InspectLibraryItem(const std::wstring& path) {
        inspectedPath_ = path;
        if (path.empty()) {
            SendState();
            return;
        }
        const auto found = std::find_if(activeRecords_.begin(), activeRecords_.end(), [&](const auto& record) {
            return record.sourcePath == path;
        });
        statusMessage_ = found == activeRecords_.end()
            ? L"The selected indexed part is no longer available in this loaded index."
            : L"Inspecting captured views for " + found->modelName;
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
        out << "{\"type\":\"similarState\",\"protocolVersion\":2";
        out << ",\"busy\":" << JsonBool(busy_);
        out << ",\"progress\":{\"done\":" << operationIndex_
            << ",\"total\":" << sources_.size()
            << ",\"indexed\":" << indexedCount_
            << ",\"skipped\":" << skippedCount_
            << ",\"failed\":" << failedCount_
            << ",\"currentModel\":" << JsonString(capture_.source.displayName)
            << ",\"viewDone\":" << capture_.viewIndex
            << ",\"viewTotal\":" << kViewCount
            << ",\"currentView\":" << (capture_.viewIndex < kViews.size() ? JsonString(kViews[capture_.viewIndex].name) : JsonString(L""))
            << ",\"message\":" << JsonString(statusMessage_) << "}";
        out << ",\"settings\":{\"folder\":" << JsonString(libraryFolder_)
            << ",\"queryImage\":" << JsonString(queryImage_)
            << ",\"recursive\":" << JsonBool(recursive_)
            << ",\"latest\":" << JsonBool(latest_)
            << ",\"topK\":" << topK_
            << ",\"autoCrop\":" << JsonBool(queryAutoCrop_) << "}";
        out << ",\"index\":{\"models\":" << activeRecords_.size()
            << ",\"views\":" << viewCount
            << ",\"viewCountPerModel\":" << kViewCount
            << ",\"cachePath\":" << JsonString(cacheDirectory_.wstring())
            << ",\"engine\":\"hybrid-shape-v2\""
            << ",\"captureProfile\":\"canonical-matrix-8-v2\"}";
        out << ",\"query\":{\"ready\":" << JsonBool(queryReady_)
            << ",\"processedPath\":" << JsonString(queryReady_ ? queryDescriptor_.normalizedPreviewPath : L"")
            << ",\"aspectRatio\":" << (queryReady_ ? queryDescriptor_.image.aspectRatio : 0.0)
            << ",\"fillRatio\":" << (queryReady_ ? queryDescriptor_.image.fillRatio : 0.0)
            << ",\"autoCrop\":" << JsonBool(queryAutoCrop_) << "}";

        out << ",\"results\":[";
        for (std::size_t i = 0; i < results_.size(); ++i) {
            if (i) out << ',';
            const auto& result = results_[i];
            out << "{\"modelName\":" << JsonString(result.modelName)
                << ",\"sourcePath\":" << JsonString(result.sourcePath)
                << ",\"previewPath\":" << JsonString(result.previewPath)
                << ",\"bestViewName\":" << JsonString(result.bestViewName)
                << ",\"score\":" << std::fixed << std::setprecision(2) << result.score
                << ",\"shapeScore\":" << result.shapeScore
                << ",\"edgeScore\":" << result.edgeScore
                << ",\"proportionScore\":" << result.proportionScore << "}";
        }
        out << "]";

        out << ",\"library\":{\"requested\":" << JsonBool(libraryRequested_)
            << ",\"filter\":" << JsonString(libraryFilter_)
            << ",\"page\":" << (libraryPage_ + 1)
            << ",\"pageSize\":" << libraryPageSize_
            << ",\"total\":" << libraryMatches_.size()
            << ",\"items\":[";
        if (libraryRequested_ && !libraryMatches_.empty()) {
            const std::size_t start = std::min(libraryMatches_.size(),
                static_cast<std::size_t>(libraryPage_) * static_cast<std::size_t>(libraryPageSize_));
            const std::size_t end = std::min(libraryMatches_.size(), start + static_cast<std::size_t>(libraryPageSize_));
            for (std::size_t item = start; item < end; ++item) {
                if (item > start) out << ',';
                const auto& record = activeRecords_[libraryMatches_[item]];
                out << "{\"modelName\":" << JsonString(record.modelName)
                    << ",\"sourcePath\":" << JsonString(record.sourcePath)
                    << ",\"previewPath\":" << JsonString(record.views.empty() ? L"" : record.views.front().renderPath)
                    << ",\"viewCount\":" << record.views.size()
                    << ",\"modifiedStamp\":" << record.modifiedStamp << "}";
            }
        }
        out << "]";

        const auto inspected = std::find_if(activeRecords_.begin(), activeRecords_.end(), [&](const auto& record) {
            return !inspectedPath_.empty() && record.sourcePath == inspectedPath_;
        });
        out << ",\"inspected\":";
        if (inspected == activeRecords_.end()) {
            out << "null";
        } else {
            out << "{\"modelName\":" << JsonString(inspected->modelName)
                << ",\"sourcePath\":" << JsonString(inspected->sourcePath)
                << ",\"views\":[";
            for (std::size_t i = 0; i < inspected->views.size(); ++i) {
                if (i) out << ',';
                const auto& view = inspected->views[i];
                out << "{\"name\":" << JsonString(view.viewName)
                    << ",\"path\":" << JsonString(view.renderPath)
                    << ",\"aspectRatio\":" << view.descriptor.aspectRatio
                    << ",\"fillRatio\":" << view.descriptor.fillRatio << "}";
            }
            out << "]}";
        }
        out << "}}";
        return out.str();
    }

    void SendState() { QueueLine(BuildStateJson()); }

    HWND hwnd_ = nullptr;
    std::wstring pipeName_;
    std::thread pipeThread_;
    std::thread writerThread_;
    std::atomic<bool> stopPipe_{false};
    std::atomic<bool> clientConnected_{false};
    HANDLE pipeHandle_ = INVALID_HANDLE_VALUE;
    std::mutex pipeMutex_;
    std::mutex writeMutex_;
    std::mutex outboundMutex_;
    std::condition_variable outboundCv_;
    std::string outboundLatest_;
    bool outboundPending_ = false;

    bool busy_ = false;
    bool recursive_ = false;
    bool latest_ = true;
    bool queryAutoCrop_ = true;
    bool queryReady_ = false;
    int topK_ = 20;
    std::size_t operationIndex_ = 0;
    std::size_t indexedCount_ = 0;
    std::size_t skippedCount_ = 0;
    std::size_t failedCount_ = 0;
    IndexStep indexStep_ = IndexStep::SelectSource;
    CaptureState capture_;
    std::wstring libraryFolder_;
    std::wstring queryImage_;
    SimilarCadQueryDescriptor queryDescriptor_;
    fs::path cacheDirectory_;
    std::wstring statusMessage_ = L"Similar CAD Search ready.";

    bool libraryRequested_ = false;
    std::wstring libraryFilter_;
    int libraryPage_ = 0;
    int libraryPageSize_ = kDefaultLibraryPageSize;
    std::vector<std::size_t> libraryMatches_;
    std::wstring inspectedPath_;

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
