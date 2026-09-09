#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace {
struct PendingWrite {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::string data;
    std::size_t offset = 0;
    bool stateMessage = false;
};

std::mutex g_queueMutex;
std::deque<PendingWrite> g_queue;

bool IsStateMessage(const char* data, DWORD size) {
    static constexpr char prefix[] = "{\"type\":\"state\"";
    const std::size_t prefixSize = sizeof(prefix) - 1;
    return data && size >= prefixSize && std::equal(prefix, prefix + prefixSize, data);
}

void ClearQueuedWrites(HANDLE pipe) {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_queue.erase(
        std::remove_if(g_queue.begin(), g_queue.end(), [pipe](const PendingWrite& item) {
            return item.pipe == pipe;
        }),
        g_queue.end());
}

void QueueWrite(HANDLE pipe, const char* data, DWORD size) {
    if (!data || size == 0) return;

    const bool stateMessage = IsStateMessage(data, size);
    std::lock_guard<std::mutex> lock(g_queueMutex);

    // Progress/result state can be produced faster than Electron paints it.
    // Keep only the newest unsent state for this connection, but preserve
    // control messages such as focus/hide/show/close in order.
    if (stateMessage) {
        g_queue.erase(
            std::remove_if(g_queue.begin(), g_queue.end(), [pipe](const PendingWrite& item) {
                return item.pipe == pipe && item.stateMessage && item.offset == 0;
            }),
            g_queue.end());
    }

    PendingWrite item;
    item.pipe = pipe;
    item.data.assign(data, data + size);
    item.stateMessage = stateMessage;
    g_queue.push_back(std::move(item));

    // Bound memory if the Electron process stalls completely. State messages
    // are already coalesced; this cap protects against repeated UI controls.
    constexpr std::size_t kMaxQueuedMessages = 128;
    while (g_queue.size() > kMaxQueuedMessages) g_queue.pop_front();
}

bool FlushOneWrite(HANDLE pipe) {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    auto it = std::find_if(g_queue.begin(), g_queue.end(), [pipe](const PendingWrite& item) {
        return item.pipe == pipe;
    });
    if (it == g_queue.end()) return true;

    DWORD written = 0;
    const DWORD remaining = static_cast<DWORD>(it->data.size() - it->offset);
    const BOOL ok = ::WriteFile(pipe,
                                it->data.data() + it->offset,
                                remaining,
                                &written,
                                nullptr);
    if (ok) {
        it->offset += written;
        if (it->offset >= it->data.size()) g_queue.erase(it);
        return true;
    }

    const DWORD error = GetLastError();
    if (error == ERROR_NO_DATA || error == ERROR_PIPE_BUSY) {
        return true;
    }

    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
        g_queue.erase(
            std::remove_if(g_queue.begin(), g_queue.end(), [pipe](const PendingWrite& item) {
                return item.pipe == pipe;
            }),
            g_queue.end());
    }
    SetLastError(error);
    return false;
}
} // namespace

BOOL WINAPI AvtElectronConnectNamedPipe(HANDLE pipe, LPOVERLAPPED overlapped) {
    // ElectronToolbox currently creates a synchronous pipe and performs the
    // blocking connection on its worker thread. That is safe. Immediately
    // after connection, switch the handle to PIPE_NOWAIT so the worker can
    // multiplex queued outbound state and inbound commands without ever
    // blocking Creo's UI thread.
    const BOOL connected = ::ConnectNamedPipe(pipe, overlapped);
    DWORD error = connected ? ERROR_SUCCESS : GetLastError();
    const bool usable = connected || error == ERROR_PIPE_CONNECTED;

    if (usable) {
        DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
        if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
            error = GetLastError();
            SetLastError(error);
            return FALSE;
        }
    }

    if (connected) return TRUE;
    SetLastError(error);
    return FALSE;
}

BOOL WINAPI AvtElectronWriteFile(HANDLE file,
                                 LPCVOID buffer,
                                 DWORD bytesToWrite,
                                 LPDWORD bytesWritten,
                                 LPOVERLAPPED overlapped) {
    // ElectronToolbox only calls this wrapper with a normal byte buffer and no
    // OVERLAPPED structure. Crucially, we do not perform kernel I/O here:
    // this function can be called on Creo's UI thread, so it merely copies the
    // message into a queue consumed by the pipe worker.
    if (overlapped != nullptr || file == INVALID_HANDLE_VALUE || (!buffer && bytesToWrite != 0)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        if (bytesWritten) *bytesWritten = 0;
        return FALSE;
    }

    QueueWrite(file, static_cast<const char*>(buffer), bytesToWrite);
    if (bytesWritten) *bytesWritten = bytesToWrite;
    return TRUE;
}

BOOL WINAPI AvtElectronReadFile(HANDLE file,
                                LPVOID buffer,
                                DWORD bytesToRead,
                                LPDWORD bytesRead,
                                LPOVERLAPPED overlapped) {
    if (overlapped != nullptr || file == INVALID_HANDLE_VALUE || !buffer || bytesToRead == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        if (bytesRead) *bytesRead = 0;
        return FALSE;
    }

    if (bytesRead) *bytesRead = 0;

    // This wrapper executes only on ElectronToolbox's pipe worker thread.
    // PIPE_NOWAIT makes the real ReadFile return immediately when no command
    // is available. Between polls we flush state/control messages queued by
    // the Creo thread. Thus all pipe kernel I/O stays off the Creo UI thread.
    for (;;) {
        if (!FlushOneWrite(file)) {
            ClearQueuedWrites(file);
            return FALSE;
        }

        DWORD read = 0;
        const BOOL ok = ::ReadFile(file, buffer, bytesToRead, &read, nullptr);
        if (ok && read > 0) {
            if (bytesRead) *bytesRead = read;
            return TRUE;
        }
        if (ok) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        const DWORD error = GetLastError();
        if (error == ERROR_NO_DATA || error == ERROR_PIPE_LISTENING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        ClearQueuedWrites(file);
        SetLastError(error);
        return FALSE;
    }
}
