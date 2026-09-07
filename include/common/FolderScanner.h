#pragma once

#include <string>
#include <vector>
#include "common/ToolTypes.h"

struct FolderScanResult {
    std::vector<ModelDescriptor> models;
    std::wstring error;
};

class FolderScanner {
public:
    static FolderScanResult Discover(const std::wstring& folder, const FolderScanOptions& options);
};
