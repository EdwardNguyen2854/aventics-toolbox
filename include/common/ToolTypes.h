#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>

#include <string>
#include <vector>

enum class QcStatus {
    Pass,
    Fail,
    Warning,
    Skip,
    Error
};

enum class ModelSourceType {
    CreoPart,
    CreoAssembly,
    CreoFamilyInstance,
    Step
};

struct ModelDescriptor {
    ModelSourceType sourceType = ModelSourceType::CreoPart;
    std::wstring displayName;
    std::wstring modelName;
    std::wstring sourcePath;
    ProMdlType modelType = PRO_MDL_UNUSED;
    int creoFileVersion = -1;

    bool isFamilyInstance = false;
    std::wstring genericName;
    std::wstring instanceName;

    ProMdl model = nullptr;
    bool alreadyInSession = false;
    bool loadedByToolbox = false;
};

struct WeakFinding {
    QcStatus status = QcStatus::Pass;
    int featureId = -1;
    std::wstring featureName;
    std::wstring featureType;
    int sectionIndex = -1;
    int dimensionId = -1;
    std::wstring details;
};

struct WeakResult {
    ProMdl model = nullptr;
    std::wstring modelName;
    std::wstring sourcePath;
    QcStatus status = QcStatus::Pass;
    std::wstring summary;
    int featuresVisited = 0;
    int sectionsFound = 0;
    int sectionsScanned = 0;
    int dimensionsChecked = 0;
    int weakDimensions = 0;
    int warnings = 0;
    int errors = 0;
    std::vector<WeakFinding> findings;
};

struct AccuracyResult {
    ProMdl model = nullptr;
    std::wstring modelName;
    std::wstring sourcePath;
    ProMdlType modelType = PRO_MDL_UNUSED;
    QcStatus status = QcStatus::Error;
    std::wstring accuracyType;
    double accuracyValue = 0.0;
    bool hasValue = false;
    std::wstring details;
};

enum class InspectionStatus {
    Added,
    Warning,
    Failed,
    Skipped
};

struct InspectionResult {
    std::wstring sourceName;
    std::wstring sourcePath;
    std::wstring addedModelName;
    std::wstring sourceKind;
    InspectionStatus status = InspectionStatus::Failed;
    int componentFeatureId = -1;
    std::wstring details;
};

enum class InstanceBuildStatus {
    Planned,
    Added,
    NotFound,
    Failed,
    Skipped
};

struct InstanceRequest {
    std::wstring code;
    int row = 1;
    int column = 1;
};

struct InstanceBuildResult {
    std::wstring requestedCode;
    int row = 1;
    int column = 1;
    std::wstring genericName;
    std::wstring sourcePath;
    std::wstring addedModelName;
    ProMdlType modelType = PRO_MDL_UNUSED;
    InstanceBuildStatus status = InstanceBuildStatus::Planned;
    int componentFeatureId = -1;
    std::wstring details;
};

struct FolderScanOptions {
    bool includeSubfolders = false;
    bool latestCreoVersionOnly = true;
    bool includeParts = true;
    bool includeAssemblies = true;
    bool includeStep = false;
};

struct InspectionOptions {
    bool includeSubfolders = false;
    bool latestCreoVersionOnly = true;
    bool includeParts = true;
    bool includeAssemblies = true;
    bool includeFamilyInstances = true;
    bool includeGenericWhenFamilyExists = false;
    bool includeStep = true;
    bool autoArrange = true;
    // false keeps the original layout; true makes completed rows advance along X.
    bool arrangeRowsAlongX = false;
    // false uses the X-Y plane; true replaces Y with Z for the arrangement plane.
    bool useZAxisForRows = false;
    int columns = 5;
    double gap = 50.0; // active assembly units
};

struct InstanceBuilderOptions {
    bool includeSubfolders = false;
    bool latestCreoVersionOnly = true;
    // false: columns advance along X and rows advance along the secondary axis.
    // true: columns advance along the secondary axis and rows advance along X.
    bool arrangeRowsAlongX = false;
    // false uses X-Y placement; true uses X-Z placement.
    bool useZAxisForRows = false;
    int columns = 5;
    // Legacy/native fallback. If row/column gap is negative, this value is used.
    double gap = 50.0;
    double columnGap = -1.0; // active assembly units, between logical columns
    double rowGap = -1.0;    // active assembly units, between logical rows
};
