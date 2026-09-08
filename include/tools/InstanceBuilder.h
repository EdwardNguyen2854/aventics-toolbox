#pragma once

#include <ProToolkit.h>
#include <ProMdl.h>
#include <ProObjects.h>

#include <string>
#include <vector>

#include "common/ToolTypes.h"

class InstanceBuilder {
public:
    InstanceBuilder(ProAssembly targetAssembly,
                    const InstanceBuilderOptions& options,
                    std::vector<InstanceRequest> requests);

    void ProcessSource(ModelDescriptor descriptor);
    void Finalize(std::vector<InstanceBuildResult>& results);
    std::size_t ResolvedCount() const;

    static bool ParseRequests(const std::wstring& text,
                              int columns,
                              std::vector<InstanceRequest>& requests,
                              std::wstring& error);
    static std::wstring StatusName(InstanceBuildStatus status);
    static bool ExportCsv(const std::vector<InstanceBuildResult>& results,
                          std::wstring& message);

private:
    struct Resolution {
        ProMdl model = nullptr;
        ProMdlType modelType = PRO_MDL_UNUSED;
        std::wstring genericName;
        std::wstring sourcePath;
        std::wstring error;
    };

    void ResolveMatchingRequests(const std::wstring& name,
                                 ProMdl model,
                                 ProMdlType modelType,
                                 const std::wstring& genericName,
                                 const std::wstring& sourcePath,
                                 const std::wstring& error = L"");
    std::vector<std::wstring> UnresolvedCodes() const;

    ProAssembly targetAssembly_ = nullptr;
    InstanceBuilderOptions options_;
    std::vector<InstanceRequest> requests_;
    std::vector<Resolution> resolutions_;
};
