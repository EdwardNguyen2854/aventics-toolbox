#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include "common/ToolTypes.h"

class AccuracyChecker {
public:
    AccuracyResult Run(ProMdl model) const;
};
