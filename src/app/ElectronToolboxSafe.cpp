// Compile the existing Electron toolbox through a narrow named-pipe shim.
// Parse all SDK/project headers first, then intercept only the three pipe calls
// used by ElectronToolbox.cpp. This avoids leaking Win32 API macros into PTC or
// standard-library headers while keeping the controller implementation intact.
#include <ProToolkit.h>
#include <ProMdl.h>
#include <ProObjects.h>

#include "app/ElectronToolbox.h"
#include "app/AppContext.h"
#include "common/FolderPicker.h"
#include "common/FolderScanner.h"
#include "common/Logger.h"
#include "common/ModelLoader.h"
#include "common/ModelUtils.h"
#include "common/SelectionService.h"
#include "common/SessionSnapshot.h"
#include "config/AppConfig.h"
#include "tools/AccuracyChecker.h"
#include "tools/InspectionBuilder.h"
#include "tools/InstanceBuilder.h"
#include "tools/WeakDimensionChecker.h"
#include "app/ElectronPipeShim.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define ConnectNamedPipe AvtElectronConnectNamedPipe
#define ReadFile AvtElectronReadFile
#define WriteFile AvtElectronWriteFile

#include "ElectronToolbox.cpp"

#undef WriteFile
#undef ReadFile
#undef ConnectNamedPipe
