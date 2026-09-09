#include <ProToolkit.h>
#ifdef AVENTICS_TYPESCRIPT_UI
#include <Windows.h>
#endif
#include "app/Commands.h"
#ifdef AVENTICS_TYPESCRIPT_UI
#include "app/WebToolbox.h"
#endif
#include "common/Logger.h"

namespace {
#ifdef AVENTICS_TYPESCRIPT_UI
bool g_webViewComInitialized = false;
#endif
}

extern "C" int user_initialize(int argc, char* argv[], char* proe_vsn, char* build) {
    (void)argc; (void)argv; (void)proe_vsn; (void)build;
    Logger::Initialize();
#ifdef AVENTICS_TYPESCRIPT_UI
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(comResult)) {
        g_webViewComInitialized = true;
    } else {
        Logger::Warn(L"Could not initialize an STA COM apartment for WebView2. The native Creo UI fallback remains available.");
    }
#endif
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
    if (g_webViewComInitialized) {
        CoUninitialize();
        g_webViewComInitialized = false;
    }
#endif
    Logger::Shutdown();
}
