#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include <ProFeature.h>

#include <string>
#include "common/ToolTypes.h"

namespace ModelUtils {
std::wstring ModelName(ProMdl model);
std::wstring FeatureName(ProFeature* feature);
std::wstring FeatureTypeName(ProFeature* feature);
std::wstring ErrorName(ProError error);
std::wstring StatusName(QcStatus status);
std::wstring InspectionStatusName(InspectionStatus status);
std::wstring ModelTypeName(ProMdlType type);
std::wstring SourceTypeName(ModelSourceType type);

bool IsPart(ProMdl model);
bool IsAssembly(ProMdl model);
bool IsSolid(ProMdl model);
bool SameModel(ProMdl a, ProMdl b);

ProError DisplayModel(ProMdl model);
ProError HighlightFeature(ProMdl model, int featureId);
ProError CurrentModel(ProMdl* model);

bool CopyToProPath(const std::wstring& text, ProPath out);
bool CopyToProMdlName(const std::wstring& text, ProMdlName out);
std::wstring ToLower(std::wstring value);
}
