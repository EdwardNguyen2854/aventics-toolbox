#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include "common/ToolTypes.h"

class WeakDimensionChecker {
public:
    WeakResult Run(ProMdl model) const;
};
