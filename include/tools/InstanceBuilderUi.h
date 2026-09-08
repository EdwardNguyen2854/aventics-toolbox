#pragma once

#include <algorithm>

namespace InstanceBuilderUi {
void SetupCallbacks(const char* dialog);
void RestoreState(const char* dialog);
void SaveState(const char* dialog);
void Refresh(const char* dialog);
void UpdateControls(const char* dialog, bool running);
}
