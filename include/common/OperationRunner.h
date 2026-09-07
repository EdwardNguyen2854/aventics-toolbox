#pragma once

#include <ProToolkit.h>
#include <ProUIDialog.h>

#include <cstddef>
#include <functional>
#include <string>

class OperationRunner {
public:
    using ProcessOne = std::function<void(std::size_t)>;
    using Progress = std::function<void(std::size_t, std::size_t)>;
    using Finished = std::function<void(bool)>;

    static OperationRunner& Instance();

    ProError Start(
        const std::string& dialog,
        std::size_t total,
        ProcessOne processOne,
        Progress progress,
        Finished finished);

    void RequestCancel();
    bool IsRunning() const;
    bool IsCancelling() const;

private:
    OperationRunner() = default;
    static void TimerAction(char* dialog, ProUITimerID timerId, ProAppData appData);
    void OnTimer(char* dialog);
    void Finish(bool cancelled);

    std::string dialog_;
    std::size_t total_ = 0;
    std::size_t index_ = 0;
    bool cancelRequested_ = false;
    bool running_ = false;
    ProUITimerID timerId_ = nullptr;
    ProcessOne processOne_;
    Progress progress_;
    Finished finished_;
};
