#pragma once

#include <string>
#include <vector>
#include "common/ToolTypes.h"

class AppContext {
public:
    static AppContext& Instance();

    std::wstring weakFolder;
    std::wstring accuracyFolder;
    std::wstring inspectionFolder;
    std::wstring instanceFolder;
    std::wstring instanceCodes;
    std::wstring weakSearch;
    std::wstring accuracySearch;
    bool weakUseSelection = true;
    bool accuracyUseSelection = true;
    bool weakIssuesOnly = false;
    bool accuracyIssuesOnly = false;
    bool weakRecursive = false;
    bool weakLatest = true;
    bool accuracyRecursive = false;
    bool accuracyLatest = true;
    bool accuracyParts = true;
    bool accuracyAssemblies = true;
    bool inspectionRecursive = false;
    bool inspectionLatest = true;
    bool inspectionParts = true;
    bool inspectionAssemblies = true;
    bool inspectionFamilyInstances = true;
    bool inspectionIncludeGeneric = false;
    bool inspectionStep = true;
    bool inspectionAutoArrange = true;
    bool inspectionRowsAlongX = false;
    bool inspectionUseZAxis = false;
    int inspectionColumns = 5;
    double inspectionGap = 50.0;
    bool instanceRecursive = false;
    bool instanceLatest = true;
    bool instanceRowsAlongX = false;
    bool instanceUseZAxis = false;
    bool instanceUnresolvedOnly = false;
    int instanceColumns = 5;
    double instanceColumnGap = 50.0;
    double instanceRowGap = 50.0;

    std::vector<WeakResult> weakResults;
    std::vector<AccuracyResult> accuracyResults;
    std::vector<InspectionResult> inspectionResults;
    std::vector<InstanceBuildResult> instanceResults;

    std::vector<ModelDescriptor> weakPending;
    std::vector<ModelDescriptor> accuracyPending;
    std::vector<ModelDescriptor> inspectionPending;
    std::vector<ModelDescriptor> instancePending;
    std::vector<ModelDescriptor> runAllPending;

    void ResetTransientOperations();

private:
    AppContext() = default;
};
