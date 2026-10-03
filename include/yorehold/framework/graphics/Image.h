#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace yh
{

// RGBA8 pixels on the CPU, rows packed tightly.
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;

    bool valid() const
    {
        if (width <= 0 || height <= 0) return false;
        const uint64_t expectedBytes = static_cast<uint64_t>(width) * height * 4;
        return rgba.size() == expectedBytes;
    }
};

// The largest side a texture may have. WebGPU guarantees 8192 on every device; bigger images
// are scaled down when loaded rather than failing on some GPUs.
constexpr int maxTextureSize = 8192;

// Image file bytes to pixels, without touching the GPU. Reads PNG, JPEG (phone photos are
// turned upright from their EXIF orientation), WebP, GIF (first frame), BMP, TGA, PSD
// (flattened) and PNM. Empty on failure.
std::optional<Image> decodeImage(std::span<const unsigned char> fileBytes);

// Smooth resampling: averages when shrinking (no shimmer), interpolates when growing.
// Alpha is premultiplied while filtering so transparent pixels don't darken edges.
Image resizeImage(const Image& image, int width, int height);
// Shrinks to fit within maxSide x maxSide, keeping the aspect ratio. Smaller images are returned as is.
Image fitImage(Image image, int maxSide);
// The centred square of the image, scaled to size x size and cut to an antialiased circle
// (token portraits).
Image circleImage(const Image& image, int size);
// The centred square, scaled to size x size (tiles from photos of any shape).
Image squareImage(const Image& image, int size);
// Cuts a tile sheet into equal cells, left to right then top to bottom. `margin` is the border
// around the sheet, `spacing` the gap between cells. Partial cells at the edges are skipped.
std::vector<Image> sliceImage(const Image& sheet, int cellWidth, int cellHeight, int margin = 0, int spacing = 0);

}
