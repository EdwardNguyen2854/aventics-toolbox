#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include <ProObjects.h>
#include "common/ToolTypes.h"

class PlacementEngine {
public:
    explicit PlacementEngine(const InspectionOptions& options);
    void Reset();
    ProError NextTransform(ProMdl model, ProMatrix matrix);

private:
    InspectionOptions options_;
    int currentColumn_ = 0;
    double currentX_ = 0.0;
    double currentY_ = 0.0;
    double rowDepth_ = 0.0;
    double rowWidth_ = 0.0;
};
