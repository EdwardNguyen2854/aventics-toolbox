#include <ProToolkit.h>
#include "tools/WeakDimensionChecker.h"
#include "common/ModelUtils.h"

#include <ProArray.h>
#include <ProFeature.h>
#include <ProSecdim.h>
#include <ProSection.h>
#include <ProSolid.h>

#include <sstream>
#include <utility>

namespace {
struct ScanContext {
    WeakResult* result = nullptr;
    int featuresVisited = 0;
    int sectionsFound = 0;
    int sectionsScanned = 0;
    int dimensionsChecked = 0;
    int weakDimensions = 0;
    int warnings = 0;
    int errors = 0;
};

bool IsSuppressed(ProFeatStatus status) {
    return status == PRO_FEAT_SUPPRESSED ||
           status == PRO_FEAT_FAMTAB_SUPPRESSED ||
           status == PRO_FEAT_SIMP_REP_SUPPRESSED ||
           status == PRO_FEAT_PROG_SUPPRESSED;
}

void AddFinding(
    ScanContext& context,
    ProFeature* feature,
    int sectionIndex,
    int dimensionId,
    QcStatus status,
    const std::wstring& details)
{
    WeakFinding finding;
    finding.status = status;
    finding.featureId = feature ? feature->id : -1;
    finding.featureName = feature ? ModelUtils::FeatureName(feature) : L"-";
    finding.featureType = feature ? ModelUtils::FeatureTypeName(feature) : L"-";
    finding.sectionIndex = sectionIndex;
    finding.dimensionId = dimensionId;
    finding.details = details;
    context.result->findings.push_back(std::move(finding));
}

ProError ScanFeature(ProFeature* feature, ProError visitStatus, ProAppData appData) {
    (void)visitStatus;
    auto* context = static_cast<ScanContext*>(appData);
    if (!context || !context->result || !feature) return PRO_TK_BAD_INPUTS;

    ++context->featuresVisited;

    ProFeatStatus status = PRO_FEAT_INVALID;
    if (ProFeatureStatusGet(feature, &status) == PRO_TK_NO_ERROR && IsSuppressed(status))
        return PRO_TK_NO_ERROR;

    int sectionCount = 0;
    ProError error = ProFeatureNumSectionsGet(feature, &sectionCount);
    if (error != PRO_TK_NO_ERROR) {
        ++context->warnings;
        AddFinding(*context, feature, -1, -1, QcStatus::Warning,
                   L"Could not query section count: " + ModelUtils::ErrorName(error));
        return PRO_TK_NO_ERROR;
    }

    if (sectionCount <= 0) return PRO_TK_NO_ERROR;
    context->sectionsFound += sectionCount;

    for (int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
        ProSection baseSection = nullptr;
        error = ProFeatureSectionCopy(feature, sectionIndex, &baseSection);
        if (error != PRO_TK_NO_ERROR || !baseSection) {
            ++context->warnings;
            AddFinding(*context, feature, sectionIndex, -1, QcStatus::Warning,
                       L"Could not copy section: " + ModelUtils::ErrorName(error));
            continue;
        }

        ++context->sectionsScanned;

        ProIntlist dimIds = nullptr;
        int dimCount = 0;
        error = ProSecdimIdsGet(baseSection, &dimIds, &dimCount);
        ProSectionFree(&baseSection);

        if (error != PRO_TK_NO_ERROR) {
            ++context->warnings;
            AddFinding(*context, feature, sectionIndex, -1, QcStatus::Warning,
                       L"Could not enumerate dimensions: " + ModelUtils::ErrorName(error));
            if (dimIds) ProArrayFree(reinterpret_cast<ProArray*>(&dimIds));
            continue;
        }

        for (int i = 0; i < dimCount; ++i) {
            const int dimId = dimIds[i];
            ++context->dimensionsChecked;

            // Always test each dimension on a fresh copy of the original feature
            // section. ProSecdimStrengthen mutates the temporary section object.
            ProSection testSection = nullptr;
            const ProError copyError = ProFeatureSectionCopy(feature, sectionIndex, &testSection);
            if (copyError != PRO_TK_NO_ERROR || !testSection) {
                ++context->warnings;
                AddFinding(*context, feature, sectionIndex, dimId, QcStatus::Warning,
                           L"Could not create isolated test copy: " + ModelUtils::ErrorName(copyError));
                continue;
            }

            const ProError strengthError = ProSecdimStrengthen(testSection, dimId);
            ProSectionFree(&testSection);

            if (strengthError == PRO_TK_NO_ERROR) {
                ++context->weakDimensions;
                AddFinding(*context, feature, sectionIndex, dimId, QcStatus::Fail,
                           L"Weak dimension detected.");
            }
            else if (strengthError == PRO_TK_E_FOUND) {
                // Already strong; no finding required.
            }
            else if (strengthError == PRO_TK_INVALID_TYPE || strengthError == PRO_TK_CANT_MODIFY) {
                ++context->warnings;
                AddFinding(*context, feature, sectionIndex, dimId, QcStatus::Warning,
                           L"Dimension could not be conclusively tested: " + ModelUtils::ErrorName(strengthError));
            }
            else {
                ++context->warnings;
                AddFinding(*context, feature, sectionIndex, dimId, QcStatus::Warning,
                           L"Weak/strong test returned: " + ModelUtils::ErrorName(strengthError));
            }
        }

        if (dimIds) ProArrayFree(reinterpret_cast<ProArray*>(&dimIds));
    }

    return PRO_TK_NO_ERROR;
}
}

WeakResult WeakDimensionChecker::Run(ProMdl model) const {
    WeakResult result;
    result.model = model;
    result.modelName = ModelUtils::ModelName(model);
    
    if (!model || !ModelUtils::IsPart(model)) {
        result.status = QcStatus::Skip;
        result.summary = L"Not a part model.";
        return result;
    }

    ScanContext context;
    context.result = &result;

    const ProError visitError = ProSolidFeatVisit(
        reinterpret_cast<ProSolid>(model),
        ScanFeature,
        nullptr,
        &context);

    if (visitError != PRO_TK_NO_ERROR && visitError != PRO_TK_E_NOT_FOUND) {
        ++context.errors;
        AddFinding(context, nullptr, -1, -1, QcStatus::Error,
                   L"Feature traversal failed: " + ModelUtils::ErrorName(visitError));
    }

    std::wstringstream summary;
    summary << context.sectionsScanned << L" section(s), "
            << context.dimensionsChecked << L" dimension(s), "
            << context.weakDimensions << L" weak";
    if (context.warnings > 0) summary << L", " << context.warnings << L" warning(s)";
    if (context.errors > 0) summary << L", " << context.errors << L" error(s)";
    result.summary = summary.str();
    result.featuresVisited = context.featuresVisited;
    result.sectionsFound = context.sectionsFound;
    result.sectionsScanned = context.sectionsScanned;
    result.dimensionsChecked = context.dimensionsChecked;
    result.weakDimensions = context.weakDimensions;
    result.warnings = context.warnings;
    result.errors = context.errors;

    if (context.errors > 0)
        result.status = QcStatus::Error;
    else if (context.weakDimensions > 0)
        result.status = QcStatus::Fail;
    else if (context.warnings > 0 || context.sectionsFound != context.sectionsScanned)
        result.status = QcStatus::Warning;
    else
        result.status = QcStatus::Pass;

    return result;
}
