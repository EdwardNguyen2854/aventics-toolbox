#include <ProToolkit.h>
#include "app/Commands.h"
#include "app/ToolboxDialog.h"
#ifdef AVENTICS_ELECTRON_UI
#include "app/ElectronToolbox.h"
#endif
#include "common/Logger.h"
#include "common/ModelUtils.h"

#include <ProArray.h>
#include <ProMenuBar.h>
#include <ProUICmd.h>
#include <ProUIMessage.h>
#include <ProUtil.h>

#include <string>

namespace {
void ShowToolboxOpenError(ProError error) {
    const std::wstring title = L"Aventics Toolbox";
    const std::wstring message =
        L"Aventics Toolbox could not open its UI.\n\n"
        L"TOOLKIT error: " + ModelUtils::ErrorName(error) +
        L"\n\nCheck %LOCALAPPDATA%\\Aventics\\AventicsToolbox\\logs\\aventics_toolbox.log for details.";

    ProUIMessageButton* buttons = nullptr;
    const ProError allocError = ProArrayAlloc(
        1,
        sizeof(ProUIMessageButton),
        1,
        reinterpret_cast<ProArray*>(&buttons));
    if (allocError != PRO_TK_NO_ERROR || !buttons) {
        Logger::Error(L"Could not allocate native error-dialog button array: " + ModelUtils::ErrorName(allocError));
        return;
    }

    buttons[0] = PRO_UI_MESSAGE_OK;
    ProUIMessageButton choice = PRO_UI_MESSAGE_OK;
    const ProError displayError = ProUIMessageDialogDisplay(
        PROUIMESSAGE_ERROR,
        const_cast<wchar_t*>(title.c_str()),
        const_cast<wchar_t*>(message.c_str()),
        buttons,
        PRO_UI_MESSAGE_OK,
        &choice);
    ProArrayFree(reinterpret_cast<ProArray*>(&buttons));

    if (displayError != PRO_TK_NO_ERROR)
        Logger::Error(L"Could not display native toolbox error dialog: " + ModelUtils::ErrorName(displayError));
}

int OpenToolbox(uiCmdCmdId, uiCmdValue*, void*) {
    ProError err = PRO_TK_GENERAL_ERROR;
#ifdef AVENTICS_ELECTRON_UI
    err = ElectronToolbox::Show();
    if (err != PRO_TK_NO_ERROR) {
        Logger::Warn(L"Electron toolbox did not open. Falling back to the native Creo UI.");
        err = ToolboxDialog::Show();
    }
#else
    err = ToolboxDialog::Show();
#endif
    if (err != PRO_TK_NO_ERROR) {
        Logger::Error(L"Toolbox UI exited with " + ModelUtils::ErrorName(err));
        ShowToolboxOpenError(err);
    }
    return 0;
}

uiCmdAccessState Access(uiCmdAccessMode) { return ACCESS_AVAILABLE; }
}

ProError RegisterAventicsToolboxCommands() {
    uiCmdCmdId command;
    ProError err = ProCmdActionAdd(
        const_cast<char*>("AVT.OpenToolbox"),
        OpenToolbox,
        uiProe2ndImmediate,
        Access,
        PRO_B_TRUE,
        PRO_B_TRUE,
        &command);
    if (err != PRO_TK_NO_ERROR) return err;

    ProFileName messageFile;
    ProStringToWstring(messageFile, const_cast<char*>("aventics_messages.txt"));
    ProCmdDesignate(command,
                    const_cast<char*>("AVT.Command.Label"),
                    const_cast<char*>("AVT.Command.Help"),
                    const_cast<char*>("AVT.Command.Description"),
                    messageFile);

    ProMenubarMenuAdd(const_cast<char*>("AVTMenu"),
                      const_cast<char*>("AVT.Menu.Label"),
                      const_cast<char*>("Info"),
                      PRO_B_TRUE,
                      messageFile);

    ProMenubarmenuPushbuttonAdd(const_cast<char*>("AVTMenu"),
                                const_cast<char*>("AVT.Open"),
                                const_cast<char*>("AVT.Command.Label"),
                                const_cast<char*>("AVT.Command.Help"),
                                nullptr,
                                PRO_B_TRUE,
                                command,
                                messageFile);
    return PRO_TK_NO_ERROR;
}
