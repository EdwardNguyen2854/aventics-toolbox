#include <ProToolkit.h>
#include "tools/PlacementEngine.h"

#include <ProSolid.h>
#include <algorithm>
#include <cmath>

namespace {
void Identity(ProMatrix matrix) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            matrix[r][c] = r == c ? 1.0 : 0.0;
}
}

PlacementEngine::PlacementEngine(const InspectionOptions& options) : options_(options) {
    if (options_.columns < 1) options_.columns = 1;
    if (options_.gap < 0.0) options_.gap = 0.0;
}

void PlacementEngine::Reset() {
    currentColumn_ = 0;
    currentX_ = 0.0;
    currentY_ = 0.0;
    rowDepth_ = 0.0;
    rowWidth_ = 0.0;
}

ProError PlacementEngine::NextTransform(ProMdl model, ProMatrix matrix) {
    if (!model) return PRO_TK_BAD_INPUTS;
    Identity(matrix);
    if (!options_.autoArrange) return PRO_TK_NO_ERROR;

    Pro3dPnt outline[2]{};
    const ProError err = ProSolidOutlineGet(reinterpret_cast<ProSolid>(model), outline);
    if (err != PRO_TK_NO_ERROR) return err;

    const double width = std::max(1.0, std::abs(outline[1][0] - outline[0][0]));
    const double secondarySpan = options_.useZAxisForRows
        ? std::max(1.0, std::abs(outline[1][2] - outline[0][2]))
        : std::max(1.0, std::abs(outline[1][1] - outline[0][1]));

    // Component translation is represented by row 3 in ProMatrix.
    // X always remains the primary layout axis. The secondary layout axis is
    // Y by default, or Z when the Inspection Z-axis option is enabled.
    matrix[3][0] = currentX_ - outline[0][0];
    if (options_.useZAxisForRows) {
        matrix[3][1] = -outline[0][1];
        matrix[3][2] = currentY_ - outline[0][2];
    } else {
        matrix[3][1] = currentY_ - outline[0][1];
        matrix[3][2] = -outline[0][2];
    }

    if (options_.arrangeRowsAlongX) {
        // Transpose the grid: items advance on the secondary axis and completed
        // rows advance on X. The secondary axis is Y or Z according to the option.
        rowWidth_ = std::max(rowWidth_, width);
        currentY_ += secondarySpan + options_.gap;
        ++currentColumn_;
        if (currentColumn_ >= options_.columns) {
            currentColumn_ = 0;
            currentY_ = 0.0;
            currentX_ += rowWidth_ + options_.gap;
            rowWidth_ = 0.0;
        }
    } else {
        rowDepth_ = std::max(rowDepth_, secondarySpan);
        currentX_ += width + options_.gap;
        ++currentColumn_;
        if (currentColumn_ >= options_.columns) {
            currentColumn_ = 0;
            currentX_ = 0.0;
            currentY_ += rowDepth_ + options_.gap;
            rowDepth_ = 0.0;
        }
    }
    return PRO_TK_NO_ERROR;
}
