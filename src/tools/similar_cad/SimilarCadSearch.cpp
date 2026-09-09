#include "tools/SimilarCadSearch.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

constexpr std::array<char, 8> kMagic = {'A', 'V', 'T', 'S', 'C', 'A', 'D', '1'};
constexpr UINT kDecodeSize = 96;
constexpr UINT kFeatureSize = 16;

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring NormalizePath(const std::wstring& input) {
    if (input.empty()) return {};
    std::error_code ec;
    fs::path path(input);
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (ec) normalized = path.lexically_normal();
    std::wstring value = normalized.wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

std::uint64_t Fnv1a64(const std::wstring& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (wchar_t c : value) {
        const std::uint32_t v = static_cast<std::uint32_t>(c);
        for (int shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<std::uint8_t>((v >> shift) & 0xffu);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::wstring Hex64(std::uint64_t value) {
    std::wostringstream out;
    out << std::hex << std::setw(16) << std::setfill(L'0') << value;
    return out.str();
}

template <typename T>
bool WritePod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return out.good();
}

template <typename T>
bool ReadPod(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}

bool WriteString(std::ofstream& out, const std::wstring& value) {
    const std::string utf8 = WideToUtf8(value);
    if (utf8.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    const auto size = static_cast<std::uint32_t>(utf8.size());
    if (!WritePod(out, size)) return false;
    if (size) out.write(utf8.data(), static_cast<std::streamsize>(size));
    return out.good();
}

bool ReadString(std::ifstream& in, std::wstring& value) {
    std::uint32_t size = 0;
    if (!ReadPod(in, size) || size > 16u * 1024u * 1024u) return false;
    std::string utf8(size, '\0');
    if (size) in.read(utf8.data(), static_cast<std::streamsize>(size));
    if (!in.good()) return false;
    value = Utf8ToWide(utf8);
    return true;
}

float Gray(const std::uint8_t* pixel) {
    const float b = static_cast<float>(pixel[0]);
    const float g = static_cast<float>(pixel[1]);
    const float r = static_cast<float>(pixel[2]);
    return (0.114f * b + 0.587f * g + 0.299f * r) / 255.0f;
}

float SampleNearest(const std::vector<float>& image, int width, int height, float x, float y) {
    const int ix = std::clamp(static_cast<int>(std::lround(x)), 0, width - 1);
    const int iy = std::clamp(static_cast<int>(std::lround(y)), 0, height - 1);
    return image[static_cast<std::size_t>(iy * width + ix)];
}

void L2Normalize(std::vector<float>& values) {
    double norm = 0.0;
    for (float value : values) norm += static_cast<double>(value) * static_cast<double>(value);
    norm = std::sqrt(norm);
    if (norm <= 1e-12) return;
    for (float& value : values) value = static_cast<float>(value / norm);
}

bool DecodeGray96(const std::wstring& imagePath, std::vector<float>& gray, std::wstring& error) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(init);

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        if (uninitialize) CoUninitialize();
        error = L"Windows Imaging Component is unavailable.";
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(imagePath.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) {
        if (uninitialize) CoUninitialize();
        error = L"Could not decode image: " + imagePath;
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) {
        if (uninitialize) CoUninitialize();
        error = L"Could not read the first image frame.";
        return false;
    }

    ComPtr<IWICBitmapScaler> scaler;
    hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(frame.Get(), kDecodeSize, kDecodeSize, WICBitmapInterpolationModeFant);
    }

    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kDecodeSize) * kDecodeSize * 4u);
    if (SUCCEEDED(hr)) {
        hr = converter->CopyPixels(nullptr, kDecodeSize * 4u,
                                   static_cast<UINT>(pixels.size()), pixels.data());
    }
    if (FAILED(hr)) {
        if (uninitialize) CoUninitialize();
        error = L"Could not convert the image for visual matching.";
        return false;
    }

    gray.assign(static_cast<std::size_t>(kDecodeSize) * kDecodeSize, 0.0f);
    for (UINT y = 0; y < kDecodeSize; ++y) {
        for (UINT x = 0; x < kDecodeSize; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * kDecodeSize + x) * 4u;
            gray[static_cast<std::size_t>(y) * kDecodeSize + x] = Gray(&pixels[offset]);
        }
    }

    if (uninitialize) CoUninitialize();
    return true;
}

float EstimateBackground(const std::vector<float>& image) {
    constexpr int patch = 8;
    double sum = 0.0;
    int count = 0;
    for (int cornerY : {0, static_cast<int>(kDecodeSize) - patch}) {
        for (int cornerX : {0, static_cast<int>(kDecodeSize) - patch}) {
            for (int y = 0; y < patch; ++y) {
                for (int x = 0; x < patch; ++x) {
                    sum += image[static_cast<std::size_t>((cornerY + y) * kDecodeSize + cornerX + x)];
                    ++count;
                }
            }
        }
    }
    return count ? static_cast<float>(sum / count) : 0.0f;
}

void DetectBounds(const std::vector<float>& image, float background, int& minX, int& minY, int& maxX, int& maxY) {
    minX = static_cast<int>(kDecodeSize);
    minY = static_cast<int>(kDecodeSize);
    maxX = -1;
    maxY = -1;

    for (int y = 1; y < static_cast<int>(kDecodeSize) - 1; ++y) {
        for (int x = 1; x < static_cast<int>(kDecodeSize) - 1; ++x) {
            const float value = image[static_cast<std::size_t>(y * kDecodeSize + x)];
            const float dx = std::abs(image[static_cast<std::size_t>(y * kDecodeSize + x + 1)] -
                                      image[static_cast<std::size_t>(y * kDecodeSize + x - 1)]);
            const float dy = std::abs(image[static_cast<std::size_t>((y + 1) * kDecodeSize + x)] -
                                      image[static_cast<std::size_t>((y - 1) * kDecodeSize + x)]);
            if (std::abs(value - background) > 0.10f || dx + dy > 0.14f) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        minX = 0;
        minY = 0;
        maxX = static_cast<int>(kDecodeSize) - 1;
        maxY = static_cast<int>(kDecodeSize) - 1;
        return;
    }

    constexpr int margin = 4;
    minX = std::max(0, minX - margin);
    minY = std::max(0, minY - margin);
    maxX = std::min(static_cast<int>(kDecodeSize) - 1, maxX + margin);
    maxY = std::min(static_cast<int>(kDecodeSize) - 1, maxY + margin);
}

} // namespace

std::filesystem::path SimilarCadSearchEngine::CacheDirectoryForLibrary(const std::wstring& libraryFolder) {
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    fs::path root = length > 0 && length < MAX_PATH
        ? fs::path(localAppData)
        : fs::temp_directory_path();
    return root / L"AventicsToolbox" / L"similar-cad" / StablePathKey(libraryFolder);
}

std::wstring SimilarCadSearchEngine::StablePathKey(const std::wstring& path) {
    return Hex64(Fnv1a64(NormalizePath(path)));
}

std::int64_t SimilarCadSearchEngine::FileStamp(const std::wstring& path) {
    std::error_code ec;
    const auto time = fs::last_write_time(fs::path(path), ec);
    if (ec) return 0;
    return static_cast<std::int64_t>(time.time_since_epoch().count());
}

bool SimilarCadSearchEngine::ComputeSignature(
    const std::wstring& imagePath,
    std::vector<float>& signature,
    std::wstring& error) {

    signature.clear();
    std::vector<float> image;
    if (!DecodeGray96(imagePath, image, error)) return false;

    const float background = EstimateBackground(image);
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    DetectBounds(image, background, minX, minY, maxX, maxY);

    signature.reserve(SignatureLength);
    const float width = static_cast<float>(std::max(1, maxX - minX));
    const float height = static_cast<float>(std::max(1, maxY - minY));

    std::vector<float> foreground;
    std::vector<float> edges;
    foreground.reserve(kFeatureSize * kFeatureSize);
    edges.reserve(kFeatureSize * kFeatureSize);

    for (UINT fy = 0; fy < kFeatureSize; ++fy) {
        for (UINT fx = 0; fx < kFeatureSize; ++fx) {
            const float x = minX + (static_cast<float>(fx) + 0.5f) * width / kFeatureSize;
            const float y = minY + (static_cast<float>(fy) + 0.5f) * height / kFeatureSize;
            const float center = SampleNearest(image, kDecodeSize, kDecodeSize, x, y);
            const float left = SampleNearest(image, kDecodeSize, kDecodeSize, x - 1.5f, y);
            const float right = SampleNearest(image, kDecodeSize, kDecodeSize, x + 1.5f, y);
            const float top = SampleNearest(image, kDecodeSize, kDecodeSize, x, y - 1.5f);
            const float bottom = SampleNearest(image, kDecodeSize, kDecodeSize, x, y + 1.5f);

            foreground.push_back(std::min(1.0f, std::abs(center - background) * 2.2f));
            const float gradient = std::sqrt((right - left) * (right - left) + (bottom - top) * (bottom - top));
            edges.push_back(std::min(1.0f, gradient * 2.5f));
        }
    }

    signature.insert(signature.end(), foreground.begin(), foreground.end());
    signature.insert(signature.end(), edges.begin(), edges.end());
    L2Normalize(signature);

    if (signature.size() != SignatureLength) {
        error = L"Internal visual-signature size mismatch.";
        signature.clear();
        return false;
    }
    return true;
}

double SimilarCadSearchEngine::Similarity(
    const std::vector<float>& left,
    const std::vector<float>& right) {
    if (left.size() != right.size() || left.empty()) return 0.0;
    double dot = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) dot += static_cast<double>(left[i]) * right[i];
    return std::clamp(dot, 0.0, 1.0);
}

bool SimilarCadSearchEngine::LoadIndex(
    const fs::path& cacheDirectory,
    std::wstring& libraryFolder,
    std::vector<SimilarCadModelRecord>& records,
    std::wstring& error) {

    libraryFolder.clear();
    records.clear();
    const fs::path indexPath = cacheDirectory / L"index.bin";
    std::ifstream in(indexPath, std::ios::binary);
    if (!in.is_open()) {
        error = L"No Similar CAD index exists for this library yet.";
        return false;
    }

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in.good() || magic != kMagic) {
        error = L"Similar CAD index header is invalid.";
        return false;
    }

    std::uint32_t schema = 0;
    std::uint32_t signatureVersion = 0;
    std::uint32_t signatureLength = 0;
    std::uint32_t recordCount = 0;
    if (!ReadPod(in, schema) || !ReadPod(in, signatureVersion) || !ReadPod(in, signatureLength) ||
        schema != IndexSchemaVersion || signatureVersion != SignatureVersion || signatureLength != SignatureLength ||
        !ReadString(in, libraryFolder) || !ReadPod(in, recordCount) || recordCount > 500000u) {
        error = L"Similar CAD index schema is incompatible. Rebuild the index.";
        return false;
    }

    records.reserve(recordCount);
    for (std::uint32_t i = 0; i < recordCount; ++i) {
        SimilarCadModelRecord record;
        std::int32_t version = -1;
        std::uint32_t viewCount = 0;
        if (!ReadString(in, record.modelName) || !ReadString(in, record.sourcePath) ||
            !ReadPod(in, version) || !ReadPod(in, record.modifiedStamp) || !ReadPod(in, viewCount) || viewCount > 32u) {
            error = L"Similar CAD index is truncated or corrupt.";
            records.clear();
            return false;
        }
        record.creoFileVersion = version;
        record.views.reserve(viewCount);
        for (std::uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex) {
            SimilarCadViewRecord view;
            if (!ReadString(in, view.renderPath)) {
                error = L"Similar CAD index is truncated or corrupt.";
                records.clear();
                return false;
            }
            view.signature.resize(SignatureLength);
            in.read(reinterpret_cast<char*>(view.signature.data()),
                    static_cast<std::streamsize>(view.signature.size() * sizeof(float)));
            if (!in.good()) {
                error = L"Similar CAD index is truncated or corrupt.";
                records.clear();
                return false;
            }
            record.views.push_back(std::move(view));
        }
        records.push_back(std::move(record));
    }

    error.clear();
    return true;
}

bool SimilarCadSearchEngine::SaveIndex(
    const fs::path& cacheDirectory,
    const std::wstring& libraryFolder,
    const std::vector<SimilarCadModelRecord>& records,
    std::wstring& error) {

    std::error_code ec;
    fs::create_directories(cacheDirectory, ec);
    fs::create_directories(cacheDirectory / L"renders", ec);
    if (ec) {
        error = L"Could not create Similar CAD cache directory: " + cacheDirectory.wstring();
        return false;
    }

    const fs::path tempPath = cacheDirectory / L"index.bin.tmp";
    const fs::path indexPath = cacheDirectory / L"index.bin";
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        error = L"Could not write Similar CAD index: " + tempPath.wstring();
        return false;
    }

    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    const std::uint32_t schema = IndexSchemaVersion;
    const std::uint32_t signatureVersion = SignatureVersion;
    const std::uint32_t signatureLength = static_cast<std::uint32_t>(SignatureLength);
    const std::uint32_t recordCount = static_cast<std::uint32_t>(records.size());
    if (!WritePod(out, schema) || !WritePod(out, signatureVersion) || !WritePod(out, signatureLength) ||
        !WriteString(out, libraryFolder) || !WritePod(out, recordCount)) {
        error = L"Could not write Similar CAD index header.";
        return false;
    }

    for (const auto& record : records) {
        const std::int32_t version = record.creoFileVersion;
        const std::uint32_t viewCount = static_cast<std::uint32_t>(record.views.size());
        if (!WriteString(out, record.modelName) || !WriteString(out, record.sourcePath) ||
            !WritePod(out, version) || !WritePod(out, record.modifiedStamp) || !WritePod(out, viewCount)) {
            error = L"Could not write Similar CAD model record.";
            return false;
        }
        for (const auto& view : record.views) {
            if (view.signature.size() != SignatureLength || !WriteString(out, view.renderPath)) {
                error = L"Could not write Similar CAD view record.";
                return false;
            }
            out.write(reinterpret_cast<const char*>(view.signature.data()),
                      static_cast<std::streamsize>(view.signature.size() * sizeof(float)));
            if (!out.good()) {
                error = L"Could not write Similar CAD signature data.";
                return false;
            }
        }
    }
    out.close();

    fs::remove(indexPath, ec);
    ec.clear();
    fs::rename(tempPath, indexPath, ec);
    if (ec) {
        error = L"Could not finalize Similar CAD index: " + indexPath.wstring();
        return false;
    }

    error.clear();
    return true;
}

bool SimilarCadSearchEngine::Search(
    const std::wstring& queryImage,
    const std::vector<SimilarCadModelRecord>& records,
    std::size_t topK,
    std::vector<SimilarCadSearchResult>& results,
    std::wstring& error) {

    results.clear();
    std::vector<float> querySignature;
    if (!ComputeSignature(queryImage, querySignature, error)) return false;

    for (const auto& record : records) {
        double best = 0.0;
        std::wstring preview;
        for (const auto& view : record.views) {
            const double score = Similarity(querySignature, view.signature);
            if (score > best || preview.empty()) {
                best = score;
                preview = view.renderPath;
            }
        }
        if (record.views.empty()) continue;
        SimilarCadSearchResult result;
        result.modelName = record.modelName;
        result.sourcePath = record.sourcePath;
        result.previewPath = preview;
        result.score = best * 100.0;
        results.push_back(std::move(result));
    }

    std::stable_sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
    });
    if (topK == 0) topK = 20;
    if (results.size() > topK) results.resize(topK);
    error.clear();
    return true;
}
