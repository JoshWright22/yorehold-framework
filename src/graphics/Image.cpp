#include "yorehold/framework/graphics/Image.h"

#include <SDL3/SDL.h>
#include <webp/decode.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR // HDR/PIC are not used for art, and HDR would need tone mapping
#define STBI_NO_PIC
#define STBI_MAX_DIMENSIONS 32768
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace yh
{

namespace
{

bool isWebp(std::span<const unsigned char> bytes)
{
    return bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
}

size_t pixelOffset(int width, int x, int y)
{
    return (static_cast<size_t>(y) * width + x) * 4;
}

// EXIF stores its numbers in either byte order. Callers check the field bounds before reading.
uint32_t read16(std::span<const unsigned char> bytes, size_t offset, bool littleEndian)
{
    const uint32_t first = bytes[offset];
    const uint32_t second = bytes[offset + 1];
    if (littleEndian) return first | (second << 8);
    return (first << 8) | second;
}

uint32_t read32(std::span<const unsigned char> bytes, size_t offset, bool littleEndian)
{
    const uint32_t first = read16(bytes, offset, littleEndian);
    const uint32_t second = read16(bytes, offset + 2, littleEndian);
    if (littleEndian) return first | (second << 16);
    return (first << 16) | second;
}

int tiffOrientation(std::span<const unsigned char> tiff)
{
    constexpr uint32_t orientationTag = 0x0112;
    constexpr size_t entryBytes = 12;
    if (tiff.size() < 8) return 1;

    const bool littleEndian = tiff[0] == 'I' && tiff[1] == 'I';
    const bool bigEndian = tiff[0] == 'M' && tiff[1] == 'M';
    if (!littleEndian && !bigEndian) return 1;
    if (read16(tiff, 2, littleEndian) != 42) return 1; // TIFF's required signature.

    const size_t directory = read32(tiff, 4, littleEndian);
    if (directory + 2 > tiff.size()) return 1;
    const uint32_t entryCount = read16(tiff, directory, littleEndian);
    for (uint32_t index = 0; index < entryCount; ++index)
    {
        const size_t entry = directory + 2 + index * entryBytes;
        if (entry + entryBytes > tiff.size()) break;
        if (read16(tiff, entry, littleEndian) == orientationTag)
        {
            const uint32_t orientation = read16(tiff, entry + 8, littleEndian);
            if (orientation >= 1 && orientation <= 8) return static_cast<int>(orientation);
            return 1;
        }
    }
    return 1;
}

// JPEG metadata lives in segments before the compressed pixels. No orientation means upright (1).
int jpegOrientation(std::span<const unsigned char> bytes)
{
    if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) return 1;

    size_t segment = 2;
    while (segment + 4 <= bytes.size() && bytes[segment] == 0xFF)
    {
        const unsigned char marker = bytes[segment + 1];
        if (marker == 0xDA || marker == 0xD9) break; // Start of pixels or end of image.

        const size_t segmentBytes = read16(bytes, segment + 2, false);
        if (segmentBytes < 2 || segment + 2 + segmentBytes > bytes.size()) break;
        const bool hasExif = marker == 0xE1 && segmentBytes >= 16
            && std::memcmp(&bytes[segment + 4], "Exif\0\0", 6) == 0;
        if (hasExif)
            return tiffOrientation(bytes.subspan(segment + 10, segmentBytes - 8));

        segment += 2 + segmentBytes;
    }
    return 1;
}

// Turns pixels stored sideways or mirrored (as phone cameras do) upright.
Image orient(Image source, int orientation)
{
    if (orientation <= 1 || orientation > 8) return source;

    const int width = source.width;
    const int height = source.height;
    const bool swapsAxes = orientation >= 5;
    Image result{swapsAxes ? height : width, swapsAxes ? width : height, std::vector<unsigned char>(source.rgba.size())};
    for (int y = 0; y < result.height; ++y)
    {
        for (int x = 0; x < result.width; ++x)
        {
            int sourceX = x;
            int sourceY = y;
            switch (orientation)
            {
            case 2: sourceX = width - 1 - x; break; // Mirror horizontally.
            case 3: sourceX = width - 1 - x; sourceY = height - 1 - y; break; // Rotate 180 degrees.
            case 4: sourceY = height - 1 - y; break; // Mirror vertically.
            case 5: sourceX = y; sourceY = x; break; // Transpose.
            case 6: sourceX = y; sourceY = height - 1 - x; break; // Rotate clockwise.
            case 7: sourceX = width - 1 - y; sourceY = height - 1 - x; break; // Transpose and mirror both axes.
            case 8: sourceX = width - 1 - y; sourceY = x; break; // Rotate anticlockwise.
            }
            const size_t destination = pixelOffset(result.width, x, y);
            const size_t origin = pixelOffset(source.width, sourceX, sourceY);
            std::memcpy(&result.rgba[destination], &source.rgba[origin], 4);
        }
    }
    return result;
}

// For each output pixel along one axis: the source pixels it blends and their weights.
struct FilterWeights
{
    std::vector<int> firstSourcePixel;
    std::vector<int> sampleCount;
    std::vector<float> weights; // Each output pixel has weightsPerPixel consecutive slots.
    int weightsPerPixel = 0;
};

FilterWeights makeFilterWeights(int sourceSize, int targetSize)
{
    const float scale = static_cast<float>(sourceSize) / static_cast<float>(targetSize);
    // Shrinking widens the tent to cover every source pixel; growing is plain linear interpolation.
    const float radius = std::max(1.0f, scale);
    FilterWeights filter;
    filter.weightsPerPixel = static_cast<int>(std::ceil(radius)) * 2 + 1;
    filter.firstSourcePixel.resize(targetSize);
    filter.sampleCount.resize(targetSize);
    filter.weights.assign(static_cast<size_t>(targetSize) * filter.weightsPerPixel, 0.0f);
    for (int pixel = 0; pixel < targetSize; ++pixel)
    {
        const float center = (static_cast<float>(pixel) + 0.5f) * scale - 0.5f;
        const int first = static_cast<int>(std::floor(center - radius)) + 1;
        const int last = std::min(first + filter.weightsPerPixel - 1, static_cast<int>(std::ceil(center + radius)) - 1);
        float weightSum = 0;
        float* weights = &filter.weights[static_cast<size_t>(pixel) * filter.weightsPerPixel];
        for (int sample = first; sample <= last; ++sample)
        {
            const float distance = std::abs(static_cast<float>(sample) - center);
            weights[sample - first] = std::max(0.0f, 1.0f - distance / radius);
            weightSum += weights[sample - first];
        }
        for (int sample = first; sample <= last; ++sample)
        {
            weights[sample - first] = weightSum > 0 ? weights[sample - first] / weightSum : 0;
        }
        filter.firstSourcePixel[pixel] = first;
        filter.sampleCount[pixel] = last - first + 1;
    }
    return filter;
}

std::vector<float> premultiplyPixels(const Image& image)
{
    const size_t pixelCount = static_cast<size_t>(image.width) * image.height;
    std::vector<float> pixels(pixelCount * 4);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        const size_t offset = pixel * 4;
        const float alpha = image.rgba[offset + 3] / 255.0f;
        for (int channel = 0; channel < 3; ++channel)
            pixels[offset + channel] = image.rgba[offset + channel] * alpha;
        pixels[offset + 3] = alpha;
    }
    return pixels;
}

std::vector<float> resampleHorizontally(const Image& image, const std::vector<float>& source, int targetWidth)
{
    const FilterWeights filter = makeFilterWeights(image.width, targetWidth);
    std::vector<float> result(static_cast<size_t>(targetWidth) * image.height * 4, 0.0f);
    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < targetWidth; ++x)
        {
            float* output = &result[pixelOffset(targetWidth, x, y)];
            const float* weights = &filter.weights[static_cast<size_t>(x) * filter.weightsPerPixel];
            for (int sample = 0; sample < filter.sampleCount[x]; ++sample)
            {
                const int sourceX = std::clamp(filter.firstSourcePixel[x] + sample, 0, image.width - 1);
                const float* input = &source[pixelOffset(image.width, sourceX, y)];
                for (int channel = 0; channel < 4; ++channel)
                    output[channel] += input[channel] * weights[sample];
            }
        }
    }
    return result;
}

Image resampleVertically(const std::vector<float>& source, int width, int sourceHeight, int targetHeight)
{
    const FilterWeights filter = makeFilterWeights(sourceHeight, targetHeight);
    Image result{width, targetHeight, std::vector<unsigned char>(static_cast<size_t>(width) * targetHeight * 4)};
    for (int y = 0; y < targetHeight; ++y)
    {
        const float* weights = &filter.weights[static_cast<size_t>(y) * filter.weightsPerPixel];
        for (int x = 0; x < width; ++x)
        {
            float blended[4] = {};
            for (int sample = 0; sample < filter.sampleCount[y]; ++sample)
            {
                const int sourceY = std::clamp(filter.firstSourcePixel[y] + sample, 0, sourceHeight - 1);
                const float* input = &source[pixelOffset(width, x, sourceY)];
                for (int channel = 0; channel < 4; ++channel)
                    blended[channel] += input[channel] * weights[sample];
            }

            // Convert back to straight alpha only after both passes; this keeps dark fringes out of cutout art.
            unsigned char* output = &result.rgba[pixelOffset(width, x, y)];
            const float alpha = std::clamp(blended[3], 0.0f, 1.0f);
            for (int channel = 0; channel < 3; ++channel)
            {
                if (alpha > 0)
                    output[channel] = static_cast<unsigned char>(std::clamp(blended[channel] / alpha, 0.0f, 255.0f) + 0.5f);
                else
                    output[channel] = 0;
            }
            output[3] = static_cast<unsigned char>(alpha * 255.0f + 0.5f);
        }
    }
    return result;
}

std::optional<Image> decodeWebp(std::span<const unsigned char> bytes)
{
    int width = 0, height = 0;
    uint8_t* pixels = WebPDecodeRGBA(bytes.data(), bytes.size(), &width, &height);
    if (!pixels)
    {
        SDL_Log("Image load failed: broken or unsupported WebP");
        return std::nullopt;
    }
    const size_t byteCount = static_cast<size_t>(width) * height * 4;
    Image image{width, height, std::vector<unsigned char>(pixels, pixels + byteCount)};
    WebPFree(pixels);
    return image;
}

std::optional<Image> decodeStbImage(std::span<const unsigned char> bytes)
{
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels)
    {
        SDL_Log("Image load failed: %s", stbi_failure_reason());
        return std::nullopt;
    }
    const size_t byteCount = static_cast<size_t>(width) * height * 4;
    Image image{width, height, std::vector<unsigned char>(pixels, pixels + byteCount)};
    stbi_image_free(pixels);
    return orient(std::move(image), jpegOrientation(bytes));
}

}

std::optional<Image> decodeImage(std::span<const unsigned char> fileBytes)
{
    if (fileBytes.empty() || fileBytes.size() > static_cast<size_t>(INT_MAX))
        return std::nullopt;

    if (isWebp(fileBytes)) return decodeWebp(fileBytes);
    return decodeStbImage(fileBytes);
}

Image resizeImage(const Image& image, int width, int height)
{
    if (width <= 0 || height <= 0 || !image.valid()) return {};
    if (width == image.width && height == image.height) return image;

    // Two one-dimensional passes are cheaper than blending both axes at once.
    const auto premultiplied = premultiplyPixels(image);
    const auto horizontal = resampleHorizontally(image, premultiplied, width);
    return resampleVertically(horizontal, width, image.height, height);
}

Image fitImage(Image image, int maxSide)
{
    if (!image.valid() || maxSide <= 0) return {};
    const int longest = std::max(image.width, image.height);
    if (longest <= maxSide) return image;
    const double scale = static_cast<double>(maxSide) / longest;
    const int fittedWidth = std::max(1, static_cast<int>(std::lround(image.width * scale)));
    const int fittedHeight = std::max(1, static_cast<int>(std::lround(image.height * scale)));
    return resizeImage(image, fittedWidth, fittedHeight);
}

Image squareImage(const Image& image, int size)
{
    if (size <= 0 || !image.valid()) return {};
    const int side = std::min(image.width, image.height);
    const int left = (image.width - side) / 2;
    const int top = (image.height - side) / 2;
    Image square{side, side, std::vector<unsigned char>(static_cast<size_t>(side) * side * 4)};
    for (int y = 0; y < side; ++y)
    {
        const size_t sourceRow = pixelOffset(image.width, left, top + y);
        const size_t destinationRow = pixelOffset(side, 0, y);
        std::memcpy(&square.rgba[destinationRow], &image.rgba[sourceRow], static_cast<size_t>(side) * 4);
    }
    return resizeImage(square, size, size);
}

Image circleImage(const Image& image, int size)
{
    Image result = squareImage(image, size);
    const float radius = size / 2.0f;
    for (int y = 0; y < result.height; ++y)
    {
        for (int x = 0; x < result.width; ++x)
        {
            // One pixel of falloff at the rim keeps the edge smooth at any zoom.
            const float distance = std::hypot(x + 0.5f - radius, y + 0.5f - radius);
            const float coverage = std::clamp(radius - distance + 0.5f, 0.0f, 1.0f);
            unsigned char& alpha = result.rgba[pixelOffset(result.width, x, y) + 3];
            alpha = static_cast<unsigned char>(alpha * coverage + 0.5f);
        }
    }
    return result;
}

std::vector<Image> sliceImage(const Image& sheet, int cellWidth, int cellHeight, int margin, int spacing)
{
    std::vector<Image> cells;
    if (!sheet.valid() || cellWidth <= 0 || cellHeight <= 0 || margin < 0 || spacing < 0) return cells;
    // Wide counters also handle a caller's very large spacing without wrapping an int.
    const int64_t right = static_cast<int64_t>(sheet.width) - margin;
    const int64_t bottom = static_cast<int64_t>(sheet.height) - margin;
    const int64_t stepX = static_cast<int64_t>(cellWidth) + spacing;
    const int64_t stepY = static_cast<int64_t>(cellHeight) + spacing;
    for (int64_t y = margin; y + cellHeight <= bottom; y += stepY)
    {
        for (int64_t x = margin; x + cellWidth <= right; x += stepX)
        {
            Image cell{cellWidth, cellHeight, std::vector<unsigned char>(static_cast<size_t>(cellWidth) * cellHeight * 4)};
            for (int row = 0; row < cellHeight; ++row)
            {
                const size_t sourceRow = pixelOffset(sheet.width, static_cast<int>(x), static_cast<int>(y) + row);
                const size_t destinationRow = pixelOffset(cellWidth, 0, row);
                std::memcpy(&cell.rgba[destinationRow], &sheet.rgba[sourceRow], static_cast<size_t>(cellWidth) * 4);
            }
            cells.push_back(std::move(cell));
        }
    }
    return cells;
}

}
