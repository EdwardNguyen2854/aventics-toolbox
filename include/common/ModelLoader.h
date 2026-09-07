#pragma once

#include <ProToolkit.h>
#include "common/ToolTypes.h"
#include "common/SessionSnapshot.h"

class ModelLoader {
public:
    static ProError Load(ModelDescriptor& descriptor, const SessionSnapshot& before);
    static ProError LoadPath(const std::wstring& path, ProMdlType type, ProMdl* model);
};
