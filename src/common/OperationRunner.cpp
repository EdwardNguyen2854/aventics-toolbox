#include <ProToolkit.h>
#include "common/OperationRunner.h"
#include "common/Logger.h"
#include "config/AppConfig.h"

#include <ProUtil.h>
#include <utility>

OperationRunner& OperationRunner::Instance() {
    static OperationRunner instance;
    return instance;
}

ProError OperationRunner::Start(const std::string& dialog, std::size_t total,
                                ProcessOne processOne, Progress progress, Finished finished) {
    if (running_) return PRO_TK_E_IN_USE;
    dialog_ = dialog;
    total_ = total;
    index_ = 0;
    cancelRequested_ = false;
    processOne_ = std::move(processOne);
    progress_ = std::move(progress);
    finished_ = std::move(finished);
    running_ = true;

    if (progress_) progress_(0, total_);
    if (total_ == 0) {
        Finish(false);
        return PRO_TK_NO_ERROR;
    }

    ProName timerName;
    ProStringToWstring(timerName, const_cast<char*>("AVT_OP_TIMER"));
    ProError err = ProUITimerCreate(TimerAction, this, timerName, &timerId_);
    if (err != PRO_TK_NO_ERROR) {
        running_ = false;
        return err;
    }
    err = ProUIDialogTimerStart(const_cast<char*>(dialog_.c_str()), timerId_, AppConfig::OperationTimerDelayMs, PRO_B_FALSE);
    if (err != PRO_TK_NO_ERROR) {
        ProUITimerDestroy(timerId_);
        timerId_ = nullptr;
        running_ = false;
    }
    return err;
}

void OperationRunner::RequestCancel() { cancelRequested_ = true; }
bool OperationRunner::IsRunning() const { return running_; }
bool OperationRunner::IsCancelling() const { return running_ && cancelRequested_; }

void OperationRunner::TimerAction(char* dialog, ProUITimerID, ProAppData appData) {
    auto* self = static_cast<OperationRunner*>(appData);
    if (self) self->OnTimer(dialog);
}

void OperationRunner::OnTimer(char* dialog) {
    if (!running_) return;
    if (cancelRequested_) { Finish(true); return; }

    if (index_ < total_ && processOne_) {
        try { processOne_(index_); }
        catch (...) { Logger::Error(L"Unhandled exception inside operation step."); }
        ++index_;
    }
    if (progress_) progress_(index_, total_);

    if (cancelRequested_ || index_ >= total_) {
        Finish(cancelRequested_);
        return;
    }

    const ProError err = ProUIDialogTimerStart(dialog, timerId_, AppConfig::OperationTimerDelayMs, PRO_B_FALSE);
    if (err != PRO_TK_NO_ERROR) {
        Logger::Error(L"Could not restart UI operation timer.");
        Finish(true);
    }
}

void OperationRunner::Finish(bool cancelled) {
    if (timerId_) {
        ProUITimerDestroy(timerId_);
        timerId_ = nullptr;
    }
    running_ = false;
    auto finished = std::move(finished_);
    processOne_ = nullptr;
    progress_ = nullptr;
    finished_ = nullptr;
    total_ = index_ = 0;
    cancelRequested_ = false;
    if (finished) finished(cancelled);
}
