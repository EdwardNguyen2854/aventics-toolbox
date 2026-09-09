#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct SimilarCadViewRecord {
    std::wstring renderPath;
    std::vector<float> signature;
};

struct SimilarCadModelRecord {
    std::wstring modelName;
    std::wstring sourcePath;
    int creoFileVersion = -1;
    std::int64_t modifiedStamp = 0;
    std::vector<SimilarCadViewRecord> views;
};

struct SimilarCadSearchResult {
    std::wstring modelName;
    std::wstring sourcePath;
    std::wstring previewPath;
    double score = 0.0;
};

class SimilarCadSearchEngine {
public:
    static constexpr std::uint32_t IndexSchemaVersion = 1;
    static constexpr std::uint32_t SignatureVersion = 1;
    static constexpr std::size_t SignatureLength = 512;

    static std::filesystem::path CacheDirectoryForLibrary(const std::wstring& libraryFolder);
    static std::wstring StablePathKey(const std::wstring& path);
    static std::int64_t FileStamp(const std::wstring& path);

    static bool ComputeSignature(
        const std::wstring& imagePath,
        std::vector<float>& signature,
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
        const std::wstring& queryImage,
        const std::vector<SimilarCadModelRecord>& records,
        std::size_t topK,
        std::vector<SimilarCadSearchResult>& results,
        std::wstring& error);
};
