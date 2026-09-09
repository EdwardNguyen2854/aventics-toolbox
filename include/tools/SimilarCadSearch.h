#pragma once

#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

struct SimilarCadImageDescriptor {
    std::vector<float> signature;
    double aspectRatio = 1.0;
    double fillRatio = 0.0;
};

struct SimilarCadViewRecord {
    std::wstring viewName;
    std::wstring renderPath;
    SimilarCadImageDescriptor descriptor;
};

struct SimilarCadModelRecord {
    std::wstring modelName;
    std::wstring sourcePath;
    int creoFileVersion = -1;
    std::int64_t modifiedStamp = 0;
    std::vector<SimilarCadViewRecord> views;
};

struct SimilarCadQueryDescriptor {
    SimilarCadImageDescriptor image;
    std::wstring sourcePath;
    std::wstring normalizedPreviewPath;
    bool autoCrop = true;
};

struct SimilarCadSearchResult {
    std::wstring modelName;
    std::wstring sourcePath;
    std::wstring previewPath;
    std::wstring bestViewName;
    double score = 0.0;
    double shapeScore = 0.0;
    double edgeScore = 0.0;
    double proportionScore = 0.0;
};

class SimilarCadSearchEngine {
public:
    static constexpr std::uint32_t IndexSchemaVersion = 2;
    static constexpr std::uint32_t SignatureVersion = 2;
    static constexpr std::size_t ShapeSignatureLength = 256;
    static constexpr std::size_t EdgeSignatureLength = 256;
    static constexpr std::size_t SignatureLength = ShapeSignatureLength + EdgeSignatureLength;

    static std::filesystem::path CacheDirectoryForLibrary(const std::wstring& libraryFolder);
    static std::filesystem::path QueryPreviewPath(const std::wstring& imagePath, bool autoCrop);
    static std::wstring StablePathKey(const std::wstring& path);
    static std::int64_t FileStamp(const std::wstring& path);

    static bool ComputeDescriptor(
        const std::wstring& imagePath,
        bool autoCrop,
        SimilarCadImageDescriptor& descriptor,
        std::wstring& error,
        const std::filesystem::path* normalizedPreviewPath = nullptr);

    static bool PrepareQuery(
        const std::wstring& imagePath,
        bool autoCrop,
        SimilarCadQueryDescriptor& descriptor,
        std::wstring& error);

    static double Similarity(
        const std::vector<float>& left,
        const std::vector<float>& right);

    static bool LoadIndex(
        const std::filesystem::path& cacheDirectory,
        std::wstring& libraryFolder,
        std::vector<SimilarCadModelRecord>& records,
        std::wstring& error);

    static bool SaveIndex(
        const std::filesystem::path& cacheDirectory,
        const std::wstring& libraryFolder,
        const std::vector<SimilarCadModelRecord>& records,
        std::wstring& error);

    static bool Search(
        const SimilarCadQueryDescriptor& query,
        const std::vector<SimilarCadModelRecord>& records,
        std::size_t topK,
        std::vector<SimilarCadSearchResult>& results,
        std::wstring& error);
};
