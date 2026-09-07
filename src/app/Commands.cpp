#include <ProToolkit.h>
#include "app/Commands.h"
#include "app/ToolboxDialog.h"
#include "common/Logger.h"
#include "common/ModelUtils.h"
#include "common/UiUtils.h"

#include <ProMenuBar.h>
#include <ProUICmd.h>
#include <ProUtil.h>

namespace {
int OpenToolbox(uiCmdCmdId, uiCmdValue*, void*) {
    const ProError err = ToolboxDialog::Show();
    if (err != PRO_TK_NO_ERROR) {
        Logger::Error(L"Toolbox dialog exited with " + ModelUtils::ErrorName(err));
        UiUtils::MessageError("AVT.Error.Dialog");
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

    // Classic menu is a development-safe launcher. The same command is also
    // available under TOOLKIT Commands for placement on a Creo ribbon tab/group.
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
