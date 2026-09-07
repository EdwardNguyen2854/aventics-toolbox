#include <ProToolkit.h>
#include "tools/AccuracyChecker.h"
#include "common/ModelUtils.h"
#include "config/QcRules.h"

#include <ProSolid.h>
#include <cmath>
#include <iomanip>
#include <sstream>

AccuracyResult AccuracyChecker::Run(ProMdl model) const {
    AccuracyResult result;
    result.model = model;
    result.modelName = ModelUtils::ModelName(model);
    if (!model || !ModelUtils::IsSolid(model)) {
        result.status = QcStatus::Skip;
        result.details = L"Accuracy applies to Creo parts and assemblies.";
        return result;
    }

    ProMdlType mdlType = PRO_MDL_UNUSED;
    ProMdlTypeGet(model, &mdlType);
    result.modelType = mdlType;

    ProAccuracyType type;
    double value = 0.0;
    const ProError err = ProSolidAccuracyGet(reinterpret_cast<ProSolid>(model), &type, &value);
    if (err != PRO_TK_NO_ERROR) {
        result.status = QcStatus::Error;
        result.details = L"Could not read model accuracy: " + ModelUtils::ErrorName(err);
        return result;
    }

    result.hasValue = true;
    result.accuracyValue = value;
    result.accuracyType = type == PRO_ACCURACY_ABSOLUTE ? L"ABSOLUTE" :
                          type == PRO_ACCURACY_RELATIVE ? L"RELATIVE" : L"UNKNOWN";

    std::wstringstream actual;
    actual << result.accuracyType << L" " << std::setprecision(12) << value;

    const bool typeOk = type == PRO_ACCURACY_ABSOLUTE;
    const bool valueOk = std::abs(value - QcRules::AccuracyTarget) <= QcRules::AccuracyComparisonTolerance;
    if (typeOk && valueOk) {
        result.status = QcStatus::Pass;
        result.details = L"ABSOLUTE 0.001";
    } else {
        result.status = QcStatus::Fail;
        result.details = L"Expected ABSOLUTE 0.001; actual " + actual.str();
    }
    return result;
}
