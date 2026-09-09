#include "tools/SimilarCadSearch.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <system_error>
#include <utility>

namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

constexpr std::array<char, 8> kMagic = {'A', 'V', 'T', 'S', 'C', 'A', 'D', '2'};
constexpr UINT kMaxDecodeSize = 192;
constexpr int kNormalizedSize = 96;
constexpr int kFeatureSize = 16;
constexpr double kTargetFill = 0.84;

struct PixelImage {
    int width = 0;
    int height = 0;
    std::vector<float> red;
    std::vector<float> green;
    std::vector<float> blue;
    std::vector<float> alpha;
    std::vector<float> gray;
};

struct Color4 {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct Bounds {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
};

struct NormalizedImage {
    std::vector<float> gray;
    std::vector<float> mask;
    double aspectRatio = 1.0;
    double fillRatio = 0.0;
};

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
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
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

float Luma(float r, float g, float b) {
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

float ColorDistance(const PixelImage& image, std::size_t index, const Color4& background) {
    const float dr = image.red[index] - background.r;
    const float dg = image.green[index] - background.g;
    const float db = image.blue[index] - background.b;
    return std::sqrt((dr * dr + dg * dg + db * db) / 3.0f);
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

bool DecodeImage(const std::wstring& imagePath, PixelImage& image, std::wstring& error) {
    image = {};
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        error = L"Could not initialize Windows imaging support.";
        return false;
    }

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
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&sourceWidth, &sourceHeight);
    if (FAILED(hr) || sourceWidth == 0 || sourceHeight == 0) {
        if (uninitialize) CoUninitialize();
        error = L"Could not read image dimensions.";
        return false;
    }

    const double scale = static_cast<double>(kMaxDecodeSize) /
                         static_cast<double>(std::max(sourceWidth, sourceHeight));
    const UINT width = std::max<UINT>(1, static_cast<UINT>(std::lround(sourceWidth * scale)));
    const UINT height = std::max<UINT>(1, static_cast<UINT>(std::lround(sourceHeight * scale)));

    ComPtr<IWICBitmapScaler> scaler;
    hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
    if (SUCCEEDED(hr)) hr = scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant);

    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    if (SUCCEEDED(hr)) {
        hr = converter->CopyPixels(nullptr, width * 4u,
                                   static_cast<UINT>(pixels.size()), pixels.data());
    }
    if (FAILED(hr)) {
        if (uninitialize) CoUninitialize();
        error = L"Could not convert the image for Similar CAD matching.";
        return false;
    }

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
    image.red.resize(count);
    image.green.resize(count);
    image.blue.resize(count);
    image.alpha.resize(count);
    image.gray.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t offset = i * 4u;
        const float b = pixels[offset] / 255.0f;
        const float g = pixels[offset + 1] / 255.0f;
        const float r = pixels[offset + 2] / 255.0f;
        const float a = pixels[offset + 3] / 255.0f;
        image.red[i] = r;
        image.green[i] = g;
        image.blue[i] = b;
        image.alpha[i] = a;
        image.gray[i] = Luma(r, g, b);
    }

    if (uninitialize) CoUninitialize();
    error.clear();
    return true;
}

Color4 EstimateBackground(const PixelImage& image) {
    const int patch = std::clamp(std::min(image.width, image.height) / 12, 2, 14);
    Color4 background{};
    background.r = background.g = background.b = background.a = 0.0f;
    int count = 0;
    for (int cornerY : {0, image.height - patch}) {
        for (int cornerX : {0, image.width - patch}) {
            for (int y = 0; y < patch; ++y) {
                for (int x = 0; x < patch; ++x) {
                    const std::size_t index = static_cast<std::size_t>((cornerY + y) * image.width + cornerX + x);
                    background.r += image.red[index];
                    background.g += image.green[index];
                    background.b += image.blue[index];
                    background.a += image.alpha[index];
                    ++count;
                }
            }
        }
    }
    if (count > 0) {
        const float inv = 1.0f / count;
        background.r *= inv;
        background.g *= inv;
        background.b *= inv;
        background.a *= inv;
    }
    return background;
}

float GrayGradient(const PixelImage& image, int x, int y) {
    const int left = std::max(0, x - 1);
    const int right = std::min(image.width - 1, x + 1);
    const int top = std::max(0, y - 1);
    const int bottom = std::min(image.height - 1, y + 1);
    const float gx = image.gray[static_cast<std::size_t>(y * image.width + right)] -
                     image.gray[static_cast<std::size_t>(y * image.width + left)];
    const float gy = image.gray[static_cast<std::size_t>(bottom * image.width + x)] -
                     image.gray[static_cast<std::size_t>(top * image.width + x)];
    return std::sqrt(gx * gx + gy * gy);
}

float AdaptiveForegroundThreshold(const PixelImage& image, const Color4& background) {
    double sum = 0.0;
    double sumSq = 0.0;
    std::size_t count = 0;
    const int stride = std::max(1, std::min(image.width, image.height) / 48);
    for (int x = 0; x < image.width; x += stride) {
        for (int y : {0, image.height - 1}) {
            const float d = ColorDistance(image, static_cast<std::size_t>(y * image.width + x), background);
            sum += d;
            sumSq += d * d;
            ++count;
        }
    }
    for (int y = 0; y < image.height; y += stride) {
        for (int x : {0, image.width - 1}) {
            const float d = ColorDistance(image, static_cast<std::size_t>(y * image.width + x), background);
            sum += d;
            sumSq += d * d;
            ++count;
        }
    }
    if (!count) return 0.12f;
    const double mean = sum / count;
    const double variance = std::max(0.0, sumSq / count - mean * mean);
    return static_cast<float>(std::clamp(mean + 1.6 * std::sqrt(variance) + 0.025, 0.07, 0.32));
}

void DilateMask(std::vector<std::uint8_t>& mask, int width, int height) {
    const auto source = mask;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            if (source[index]) continue;
            bool neighbor = false;
            for (int dy = -1; dy <= 1 && !neighbor; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (source[static_cast<std::size_t>((y + dy) * width + x + dx)]) {
                        neighbor = true;
                        break;
                    }
                }
            }
            if (neighbor) mask[index] = 1;
        }
    }
}

bool FindForegroundComponent(
    const PixelImage& image,
    const Color4& background,
    float threshold,
    std::vector<float>& softMask,
    Bounds& bounds) {

    const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
    std::vector<std::uint8_t> candidate(count, 0);
    const bool transparentBackground = background.a < 0.35f;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * image.width + x);
            const float distance = ColorDistance(image, index, background);
            const float gradient = GrayGradient(image, x, y);
            const bool foreground = transparentBackground
                ? image.alpha[index] > 0.15f
                : (distance > threshold || gradient > 0.16f);
            candidate[index] = foreground ? 1 : 0;
        }
    }

    DilateMask(candidate, image.width, image.height);

    std::vector<int> labels(count, -1);
    struct Component {
        int area = 0;
        int minX = 0;
        int minY = 0;
        int maxX = 0;
        int maxY = 0;
        bool touchesBorder = false;
        double score = 0.0;
    };
    std::vector<Component> components;
    constexpr std::array<int, 4> dx = {1, -1, 0, 0};
    constexpr std::array<int, 4> dy = {0, 0, 1, -1};

    for (int startY = 0; startY < image.height; ++startY) {
        for (int startX = 0; startX < image.width; ++startX) {
            const std::size_t startIndex = static_cast<std::size_t>(startY * image.width + startX);
            if (!candidate[startIndex] || labels[startIndex] >= 0) continue;

            const int label = static_cast<int>(components.size());
            Component component;
            component.minX = component.maxX = startX;
            component.minY = component.maxY = startY;
            double sumX = 0.0;
            double sumY = 0.0;
            std::queue<std::pair<int, int>> queue;
            queue.push({startX, startY});
            labels[startIndex] = label;

            while (!queue.empty()) {
                const auto [x, y] = queue.front();
                queue.pop();
                ++component.area;
                sumX += x;
                sumY += y;
                component.minX = std::min(component.minX, x);
                component.minY = std::min(component.minY, y);
                component.maxX = std::max(component.maxX, x);
                component.maxY = std::max(component.maxY, y);
                if (x == 0 || y == 0 || x == image.width - 1 || y == image.height - 1)
                    component.touchesBorder = true;

                for (std::size_t n = 0; n < dx.size(); ++n) {
                    const int nx = x + dx[n];
                    const int ny = y + dy[n];
                    if (nx < 0 || ny < 0 || nx >= image.width || ny >= image.height) continue;
                    const std::size_t next = static_cast<std::size_t>(ny * image.width + nx);
                    if (!candidate[next] || labels[next] >= 0) continue;
                    labels[next] = label;
                    queue.push({nx, ny});
                }
            }

            const double cx = component.area ? sumX / component.area : startX;
            const double cy = component.area ? sumY / component.area : startY;
            const double imageCx = (image.width - 1) * 0.5;
            const double imageCy = (image.height - 1) * 0.5;
            const double distance = std::hypot(cx - imageCx, cy - imageCy);
            const double maxDistance = std::max(1.0, std::hypot(imageCx, imageCy));
            const double centerWeight = 1.0 - std::min(1.0, distance / maxDistance);
            const double borderWeight = component.touchesBorder ? 0.45 : 1.0;
            component.score = component.area * borderWeight * (0.65 + 0.35 * centerWeight);
            components.push_back(component);
        }
    }

    if (components.empty()) return false;
    int best = 0;
    for (int i = 1; i < static_cast<int>(components.size()); ++i) {
        if (components[i].score > components[best].score) best = i;
    }
    if (components[best].area < std::max(8, static_cast<int>(count / 400))) return false;

    bounds.minX = components[best].minX;
    bounds.minY = components[best].minY;
    bounds.maxX = components[best].maxX;
    bounds.maxY = components[best].maxY;
    const int margin = std::max(2, std::min(bounds.maxX - bounds.minX + 1,
                                            bounds.maxY - bounds.minY + 1) / 24);
    bounds.minX = std::max(0, bounds.minX - margin);
    bounds.minY = std::max(0, bounds.minY - margin);
    bounds.maxX = std::min(image.width - 1, bounds.maxX + margin);
    bounds.maxY = std::min(image.height - 1, bounds.maxY + margin);

    softMask.assign(count, 0.0f);
    for (int y = bounds.minY; y <= bounds.maxY; ++y) {
        for (int x = bounds.minX; x <= bounds.maxX; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * image.width + x);
            if (transparentBackground) {
                softMask[index] = std::clamp(image.alpha[index], 0.0f, 1.0f);
                continue;
            }
            const float distance = ColorDistance(image, index, background);
            const float soft = std::clamp((distance - threshold * 0.35f) /
                                          std::max(0.04f, threshold * 0.75f), 0.0f, 1.0f);
            const float componentBoost = labels[index] == best ? 0.55f : 0.0f;
            softMask[index] = std::clamp(std::max(soft, componentBoost), 0.0f, 1.0f);
        }
    }
    return true;
}

NormalizedImage NormalizeObject(
    const PixelImage& image,
    const Color4& background,
    bool autoCrop) {

    Bounds bounds{0, 0, image.width - 1, image.height - 1};
    std::vector<float> sourceMask;
    const float threshold = AdaptiveForegroundThreshold(image, background);
    Bounds detected = bounds;
    const bool detectedForeground = FindForegroundComponent(image, background, threshold, sourceMask, detected);
    if (autoCrop && detectedForeground) bounds = detected;

    const std::size_t sourceCount = static_cast<std::size_t>(image.width) * image.height;
    if (sourceMask.size() != sourceCount) {
        sourceMask.assign(sourceCount, 0.0f);
        for (std::size_t i = 0; i < sourceCount; ++i) {
            if (background.a < 0.35f) sourceMask[i] = image.alpha[i];
            else {
                const float distance = ColorDistance(image, i, background);
                sourceMask[i] = std::clamp((distance - threshold * 0.25f) /
                                           std::max(0.04f, threshold), 0.0f, 1.0f);
            }
        }
    }

    const int cropWidth = std::max(1, bounds.maxX - bounds.minX + 1);
    const int cropHeight = std::max(1, bounds.maxY - bounds.minY + 1);
    const double scale = (kTargetFill * kNormalizedSize) / std::max(cropWidth, cropHeight);
    const double destWidth = std::max(1.0, cropWidth * scale);
    const double destHeight = std::max(1.0, cropHeight * scale);
    const double left = (kNormalizedSize - destWidth) * 0.5;
    const double top = (kNormalizedSize - destHeight) * 0.5;

    NormalizedImage normalized;
    normalized.gray.assign(static_cast<std::size_t>(kNormalizedSize) * kNormalizedSize,
                           Luma(background.r, background.g, background.b));
    normalized.mask.assign(normalized.gray.size(), 0.0f);
    normalized.aspectRatio = static_cast<double>(cropWidth) / cropHeight;

    double maskSum = 0.0;
    for (int y = 0; y < kNormalizedSize; ++y) {
        for (int x = 0; x < kNormalizedSize; ++x) {
            const double u = (x + 0.5 - left) / destWidth;
            const double v = (y + 0.5 - top) / destHeight;
            if (u < 0.0 || v < 0.0 || u >= 1.0 || v >= 1.0) continue;
            const float sourceX = static_cast<float>(bounds.minX + u * (cropWidth - 1));
            const float sourceY = static_cast<float>(bounds.minY + v * (cropHeight - 1));
            const std::size_t index = static_cast<std::size_t>(y * kNormalizedSize + x);
            normalized.gray[index] = SampleNearest(image.gray, image.width, image.height, sourceX, sourceY);
            normalized.mask[index] = SampleNearest(sourceMask, image.width, image.height, sourceX, sourceY);
            maskSum += normalized.mask[index];
        }
    }
    normalized.fillRatio = maskSum / normalized.mask.size();
    return normalized;
}

bool WritePreviewBmp(const fs::path& path, const NormalizedImage& image, std::wstring& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = L"Could not create query-preview directory.";
        return false;
    }

    constexpr int bytesPerPixel = 3;
    const int rawStride = kNormalizedSize * bytesPerPixel;
    const int stride = (rawStride + 3) & ~3;
    const DWORD pixelBytes = static_cast<DWORD>(stride * kNormalizedSize);

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;

    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = kNormalizedSize;
    infoHeader.biHeight = kNormalizedSize;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = pixelBytes;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        error = L"Could not write normalized query preview.";
        return false;
    }
    out.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    out.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    std::vector<std::uint8_t> row(static_cast<std::size_t>(stride), 245);
    for (int y = kNormalizedSize - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 245);
        for (int x = 0; x < kNormalizedSize; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * kNormalizedSize + x);
            const float mask = std::clamp(image.mask[index], 0.0f, 1.0f);
            const int sourceGray = std::clamp(static_cast<int>(std::lround(image.gray[index] * 255.0f)), 0, 255);
            const int value = std::clamp(static_cast<int>(245.0f * (1.0f - mask) + sourceGray * mask), 0, 255);
            const std::size_t offset = static_cast<std::size_t>(x * bytesPerPixel);
            row[offset] = row[offset + 1] = row[offset + 2] = static_cast<std::uint8_t>(value);
        }
        out.write(reinterpret_cast<const char*>(row.data()), stride);
    }
    if (!out.good()) {
        error = L"Could not finish normalized query preview.";
        return false;
    }
    error.clear();
    return true;
}

void BuildSignature(const NormalizedImage& image, std::vector<float>& signature) {
    std::vector<float> shape;
    std::vector<float> edges;
    shape.reserve(SimilarCadSearchEngine::ShapeSignatureLength);
    edges.reserve(SimilarCadSearchEngine::EdgeSignatureLength);
    constexpr int block = kNormalizedSize / kFeatureSize;

    for (int cellY = 0; cellY < kFeatureSize; ++cellY) {
        for (int cellX = 0; cellX < kFeatureSize; ++cellX) {
            double shapeSum = 0.0;
            double edgeSum = 0.0;
            for (int y = cellY * block; y < (cellY + 1) * block; ++y) {
                for (int x = cellX * block; x < (cellX + 1) * block; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y * kNormalizedSize + x);
                    shapeSum += image.mask[index];
                    const int left = std::max(0, x - 1);
                    const int right = std::min(kNormalizedSize - 1, x + 1);
                    const int top = std::max(0, y - 1);
                    const int bottom = std::min(kNormalizedSize - 1, y + 1);
                    const float gx = image.gray[static_cast<std::size_t>(y * kNormalizedSize + right)] -
                                     image.gray[static_cast<std::size_t>(y * kNormalizedSize + left)];
                    const float gy = image.gray[static_cast<std::size_t>(bottom * kNormalizedSize + x)] -
                                     image.gray[static_cast<std::size_t>(top * kNormalizedSize + x)];
                    const float mgx = image.mask[static_cast<std::size_t>(y * kNormalizedSize + right)] -
                                      image.mask[static_cast<std::size_t>(y * kNormalizedSize + left)];
                    const float mgy = image.mask[static_cast<std::size_t>(bottom * kNormalizedSize + x)] -
                                      image.mask[static_cast<std::size_t>(top * kNormalizedSize + x)];
                    const float intensityEdge = std::sqrt(gx * gx + gy * gy);
                    const float silhouetteEdge = std::sqrt(mgx * mgx + mgy * mgy);
                    edgeSum += std::min(1.0f, intensityEdge * 1.8f + silhouetteEdge * 0.85f);
                }
            }
            const double divisor = block * block;
            shape.push_back(static_cast<float>(shapeSum / divisor));
            edges.push_back(static_cast<float>(edgeSum / divisor));
        }
    }

    L2Normalize(shape);
    L2Normalize(edges);
    signature.clear();
    signature.reserve(SimilarCadSearchEngine::SignatureLength);
    signature.insert(signature.end(), shape.begin(), shape.end());
    signature.insert(signature.end(), edges.begin(), edges.end());
}

double SliceSimilarity(const std::vector<float>& left, const std::vector<float>& right,
                       std::size_t start, std::size_t count) {
    if (left.size() < start + count || right.size() < start + count) return 0.0;
    double dot = 0.0;
    for (std::size_t i = 0; i < count; ++i)
        dot += static_cast<double>(left[start + i]) * right[start + i];
    return std::clamp(dot, 0.0, 1.0);
}

double ProportionSimilarity(const SimilarCadImageDescriptor& left, const SimilarCadImageDescriptor& right) {
    const double leftAspect = std::max(0.05, left.aspectRatio);
    const double rightAspect = std::max(0.05, right.aspectRatio);
    const double aspect = std::exp(-1.45 * std::abs(std::log(leftAspect / rightAspect)));
    const double fill = 1.0 - std::min(1.0, std::abs(left.fillRatio - right.fillRatio) * 2.2);
    return std::clamp(0.82 * aspect + 0.18 * fill, 0.0, 1.0);
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

std::filesystem::path SimilarCadSearchEngine::QueryPreviewPath(const std::wstring& imagePath, bool autoCrop) {
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    fs::path root = length > 0 && length < MAX_PATH
        ? fs::path(localAppData)
        : fs::temp_directory_path();
    const std::wstring identity = NormalizePath(imagePath) + L"|" +
        std::to_wstring(FileStamp(imagePath)) + (autoCrop ? L"|auto" : L"|full");
    return root / L"AventicsToolbox" / L"similar-cad" / L"query-previews" /
           (Hex64(Fnv1a64(identity)) + L".bmp");
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

bool SimilarCadSearchEngine::ComputeDescriptor(
    const std::wstring& imagePath,
    bool autoCrop,
    SimilarCadImageDescriptor& descriptor,
    std::wstring& error,
    const fs::path* normalizedPreviewPath) {

    descriptor = {};
    PixelImage image;
    if (!DecodeImage(imagePath, image, error)) return false;
    const Color4 background = EstimateBackground(image);
    const NormalizedImage normalized = NormalizeObject(image, background, autoCrop);
    BuildSignature(normalized, descriptor.signature);
    descriptor.aspectRatio = normalized.aspectRatio;
    descriptor.fillRatio = normalized.fillRatio;

    if (descriptor.signature.size() != SignatureLength) {
        error = L"Internal Similar CAD descriptor size mismatch.";
        descriptor = {};
        return false;
    }
    if (normalizedPreviewPath && !WritePreviewBmp(*normalizedPreviewPath, normalized, error)) {
        descriptor = {};
        return false;
    }
    error.clear();
    return true;
}

bool SimilarCadSearchEngine::PrepareQuery(
    const std::wstring& imagePath,
    bool autoCrop,
    SimilarCadQueryDescriptor& descriptor,
    std::wstring& error) {

    descriptor = {};
    if (imagePath.empty()) {
        error = L"Choose a query image first.";
        return false;
    }
    const fs::path previewPath = QueryPreviewPath(imagePath, autoCrop);
    if (!ComputeDescriptor(imagePath, autoCrop, descriptor.image, error, &previewPath)) return false;
    descriptor.sourcePath = imagePath;
    descriptor.normalizedPreviewPath = previewPath.wstring();
    descriptor.autoCrop = autoCrop;
    return true;
}

double SimilarCadSearchEngine::Similarity(
    const std::vector<float>& left,
    const std::vector<float>& right) {
    if (left.size() != right.size() || left.empty()) return 0.0;
    const double shape = SliceSimilarity(left, right, 0, ShapeSignatureLength);
    const double edge = SliceSimilarity(left, right, ShapeSignatureLength, EdgeSignatureLength);
    return std::clamp(0.64 * shape + 0.36 * edge, 0.0, 1.0);
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
        error = L"Similar CAD index was created by an older capture/query engine. Rebuild the index.";
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
            !ReadPod(in, version) || !ReadPod(in, record.modifiedStamp) || !ReadPod(in, viewCount) || viewCount > 64u) {
            error = L"Similar CAD index is truncated or corrupt.";
            records.clear();
            return false;
        }
        record.creoFileVersion = version;
        record.views.reserve(viewCount);
        for (std::uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex) {
            SimilarCadViewRecord view;
            if (!ReadString(in, view.viewName) || !ReadString(in, view.renderPath) ||
                !ReadPod(in, view.descriptor.aspectRatio) || !ReadPod(in, view.descriptor.fillRatio)) {
                error = L"Similar CAD index is truncated or corrupt.";
                records.clear();
                return false;
            }
            view.descriptor.signature.resize(SignatureLength);
            in.read(reinterpret_cast<char*>(view.descriptor.signature.data()),
                    static_cast<std::streamsize>(view.descriptor.signature.size() * sizeof(float)));
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
            if (view.descriptor.signature.size() != SignatureLength ||
                !WriteString(out, view.viewName) || !WriteString(out, view.renderPath) ||
                !WritePod(out, view.descriptor.aspectRatio) || !WritePod(out, view.descriptor.fillRatio)) {
                error = L"Could not write Similar CAD view record.";
                return false;
            }
            out.write(reinterpret_cast<const char*>(view.descriptor.signature.data()),
                      static_cast<std::streamsize>(view.descriptor.signature.size() * sizeof(float)));
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
    const SimilarCadQueryDescriptor& query,
    const std::vector<SimilarCadModelRecord>& records,
    std::size_t topK,
    std::vector<SimilarCadSearchResult>& results,
    std::wstring& error) {

    results.clear();
    if (query.image.signature.size() != SignatureLength) {
        error = L"The query image has not been prepared for Similar CAD Search.";
        return false;
    }

    struct ViewScore {
        const SimilarCadViewRecord* view = nullptr;
        double composite = 0.0;
        double shape = 0.0;
        double edge = 0.0;
        double proportion = 0.0;
    };

    for (const auto& record : records) {
        std::vector<ViewScore> scores;
        scores.reserve(record.views.size());
        for (const auto& view : record.views) {
            if (view.descriptor.signature.size() != SignatureLength) continue;
            ViewScore score;
            score.view = &view;
            score.shape = SliceSimilarity(query.image.signature, view.descriptor.signature,
                                          0, ShapeSignatureLength);
            score.edge = SliceSimilarity(query.image.signature, view.descriptor.signature,
                                         ShapeSignatureLength, EdgeSignatureLength);
            score.proportion = ProportionSimilarity(query.image, view.descriptor);
            score.composite = std::clamp(0.56 * score.shape + 0.29 * score.edge +
                                         0.15 * score.proportion, 0.0, 1.0);
            scores.push_back(score);
        }
        if (scores.empty()) continue;
        std::stable_sort(scores.begin(), scores.end(), [](const auto& left, const auto& right) {
            return left.composite > right.composite;
        });

        constexpr std::array<double, 3> weights = {0.82, 0.12, 0.06};
        double aggregate = 0.0;
        double weightSum = 0.0;
        for (std::size_t i = 0; i < std::min(scores.size(), weights.size()); ++i) {
            aggregate += scores[i].composite * weights[i];
            weightSum += weights[i];
        }
        if (weightSum > 0.0) aggregate /= weightSum;

        SimilarCadSearchResult result;
        result.modelName = record.modelName;
        result.sourcePath = record.sourcePath;
        result.previewPath = scores.front().view->renderPath;
        result.bestViewName = scores.front().view->viewName;
        result.score = aggregate * 100.0;
        result.shapeScore = scores.front().shape * 100.0;
        result.edgeScore = scores.front().edge * 100.0;
        result.proportionScore = scores.front().proportion * 100.0;
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
