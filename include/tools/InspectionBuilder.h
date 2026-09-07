#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include <vector>
#include <set>
#include "common/ToolTypes.h"
#include "tools/PlacementEngine.h"

class InspectionBuilder {
public:
    InspectionBuilder(ProAssembly targetAssembly, const InspectionOptions& options);
    void Process(const ModelDescriptor& descriptor, std::vector<InspectionResult>& results);

private:
    void ProcessNative(ModelDescriptor descriptor, std::vector<InspectionResult>& results);
    void ProcessStep(const ModelDescriptor& descriptor, std::vector<InspectionResult>& results);
    void AssembleOne(ProMdl model, const std::wstring& sourceName, const std::wstring& sourcePath,
                     const std::wstring& kind, std::vector<InspectionResult>& results);

    ProAssembly targetAssembly_ = nullptr;
    InspectionOptions options_;
    PlacementEngine placement_;
    std::set<std::wstring> addedKeys_;
};
