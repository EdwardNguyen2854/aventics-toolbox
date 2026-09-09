#include <ProToolkit.h>
#include "app/Commands.h"
#ifdef AVENTICS_ELECTRON_UI
#include "app/ElectronToolbox.h"
#include "tools/SimilarCadSearchHost.h"
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
#ifdef AVENTICS_ELECTRON_UI
    if (SimilarCadSearchHost::Start() != PRO_TK_NO_ERROR) {
        Logger::Error(L"Similar CAD Search host could not start.");
    }
#endif
    return 0;
}

extern "C" void user_terminate() {
#ifdef AVENTICS_ELECTRON_UI
    SimilarCadSearchHost::Shutdown();
    ElectronToolbox::Shutdown();
#endif
    Logger::Shutdown();
}
