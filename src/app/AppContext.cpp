#include <ProToolkit.h>
#include "app/AppContext.h"

AppContext& AppContext::Instance() {
    static AppContext instance;
    return instance;
}

void AppContext::ResetTransientOperations() {
    weakPending.clear();
    accuracyPending.clear();
    inspectionPending.clear();
    runAllPending.clear();
}
