#include <ProToolkit.h>
#include "app/Commands.h"
#ifdef AVENTICS_TYPESCRIPT_UI
#include "app/WebToolbox.h"
#endif
#include "common/Logger.h"

extern "C" int user_initialize(int argc, char* argv[], char* proe_vsn, char* build) {
    (void)argc; (void)argv; (void)proe_vsn; (void)build;
    Logger::Initialize();
    const ProError err = RegisterAventicsToolboxCommands();
    if (err != PRO_TK_NO_ERROR) {
        Logger::Error(L"Command registration failed.");
        return static_cast<int>(err);
    }
    return 0;
}

extern "C" void user_terminate() {
#ifdef AVENTICS_TYPESCRIPT_UI
    WebToolbox::Shutdown();
#endif
    Logger::Shutdown();
}
