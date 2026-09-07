#pragma once

#include <ProToolkit.h>
#include <vector>
#include "common/ToolTypes.h"

class SelectionService {
public:
    static ProError SelectModels(bool includeParts, bool includeAssemblies, std::vector<ModelDescriptor>& outModels);
};
