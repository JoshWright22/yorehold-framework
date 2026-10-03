#include "Checks.h"
#include "ImageTests.h"

#include <yorehold/framework/graphics/Image.h>
#include <yorehold/framework/map/TileMap.h>

#include <array>
#include <limits>
#include <span>
#include <vector>
#include <webp/encode.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace regression
{
namespace
{

yh::Image rgbaFixture()
{
    return {2, 2, {
        240, 80, 20, 255,   20, 180, 60, 128,
        40, 60, 200, 255,   0, 0, 0, 0,
    }};
}

void appendEncodedBytes(void* context, void* data, int size)
{
    auto& bytes = *static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes.insert(bytes.end(), begin, begin + size);
}

std::vector<unsigned char> jpegWithOrientation(std::span<const unsigned char> jpeg, int orientation, bool littleEndian)
{
    // Insert an EXIF segment after the JPEG start marker, keeping its compressed pixels untouched.
    std::vector<unsigned char> bytes{0xFF, 0xD8, 0xFF, 0xE1, 0, 34, 'E', 'x', 'i', 'f', 0, 0};
    auto append16 = [&](unsigned value) {
        bytes.push_back(static_cast<unsigned char>(littleEndian ? value : value >> 8));
        bytes.push_back(static_cast<unsigned char>(littleEndian ? value >> 8 : value));
    };
    auto append32 = [&](unsigned value) {
        if (littleEndian)
        {
            append16(value & 0xFFFF);
            append16(value >> 16);
        }
        else
        {
            append16(value >> 16);
            append16(value & 0xFFFF);
        }
    };

    const unsigned char byteOrder = littleEndian ? 'I' : 'M';
    bytes.push_back(byteOrder);
    bytes.push_back(byteOrder);
    append16(42);          // TIFF signature.
    append32(8);           // Offset to the first metadata directory.
    append16(1);           // One directory entry.
    append16(0x0112);      // Orientation tag.
    append16(3);           // Value type: 16-bit integer.
    append32(1);           // One value.
    append16(orientation);
    append16(0);           // Unused bytes in the value field.
    append32(0);           // No next directory.
    bytes.insert(bytes.end(), jpeg.begin() + 2, jpeg.end());
    return bytes;
}

void imageFormats()
{
    const yh::Image source = rgbaFixture();
    std::vector<unsigned char> png, bmp, tga;
    CHECK(stbi_write_png_to_func(appendEncodedBytes, &png, 2, 2, 4, source.rgba.data(), 8));
    CHECK(stbi_write_bmp_to_func(appendEncodedBytes, &bmp, 2, 2, 4, source.rgba.data()));
    CHECK(stbi_write_tga_to_func(appendEncodedBytes, &tga, 2, 2, 4, source.rgba.data()));
    for (const auto* bytes : {&png, &bmp, &tga})
    {
        const auto decoded = yh::decodeImage(*bytes);
        CHECK(decoded && decoded->valid() && decoded->width == 2 && decoded->height == 2);
        CHECK(decoded && decoded->rgba == source.rgba);
    }
    uint8_t* webp = nullptr;
    const size_t webpSize = WebPEncodeLosslessRGBA(source.rgba.data(), 2, 2, 8, &webp);
    CHECK(webpSize > 0 && webp);
    if (webp)
    {
        const auto decoded = yh::decodeImage(std::span<const unsigned char>(webp, webpSize));
        CHECK(decoded && decoded->rgba == source.rgba);
        WebPFree(webp);
    }

    const std::vector<unsigned char> gif{'G','I','F','8','9','a',1,0,1,0,0x80,0,0, 255,0,0,0,0,0,
                                       0x2C,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3B};
    const auto firstFrame = yh::decodeImage(gif);
    CHECK(firstFrame && firstFrame->rgba == std::vector<unsigned char>{255,0,0,255});
    const std::vector<unsigned char> pnm{'P','6','\n','2',' ','1','\n','2','5','5','\n',255,0,0,0,255,0};
    const auto portable = yh::decodeImage(pnm);
    CHECK(portable && portable->width == 2 && portable->rgba == std::vector<unsigned char>{255,0,0,255, 0,255,0,255});
    CHECK(!yh::decodeImage({}) && !yh::decodeImage(std::vector<unsigned char>{1,2,3,4}));
    CHECK(!yh::decodeImage(std::span<const unsigned char>(png.data(), 20)));
    CHECK(!yh::decodeImage(std::vector<unsigned char>{'R','I','F','F',0,0,0,0,'W','E','B','P'}));
}

void jpegOrientations()
{
    // Phone JPEGs: all eight EXIF orientations preserve the decoded pixels in either byte order.
    const yh::Image photo{3, 2, {240,80,20,255, 20,180,60,255, 40,60,200,255,
                               80,100,120,255, 160,180,200,255, 220,200,180,255}};
    std::vector<unsigned char> jpg;
    CHECK(stbi_write_jpg_to_func(appendEncodedBytes, &jpg, 3, 2, 4, photo.rgba.data(), 95));
    const auto original = yh::decodeImage(jpg);
    CHECK(original && original->width == 3 && original->height == 2);
    if (original)
    {
        const int expected[8][6] = {{0,1,2,3,4,5}, {2,1,0,5,4,3}, {5,4,3,2,1,0}, {3,4,5,0,1,2},
                                   {0,3,1,4,2,5}, {3,0,4,1,5,2}, {5,2,4,1,3,0}, {2,5,1,4,0,3}};
        for (int orientation = 1; orientation <= 8; ++orientation)
        {
            for (bool little : {true, false})
            {
                const auto tagged = jpegWithOrientation(jpg, orientation, little);
                const auto turned = yh::decodeImage(tagged);
                CHECK(turned && turned->width == (orientation >= 5 ? 2 : 3) && turned->height == (orientation >= 5 ? 3 : 2));
                if (turned)
                    for (size_t i = 0; i < 6; ++i)
                        for (size_t c = 0; c < 4; ++c)
                            CHECK(turned->rgba[i * 4 + c] == original->rgba[static_cast<size_t>(expected[orientation - 1][i]) * 4 + c]);
            }
        }
    }
}

void imageTransforms()
{
    const yh::Image source = rgbaFixture();
    // Resampling keeps colour at transparent edges instead of blending in invisible black.
    const yh::Image edge{2, 1, {255,80,20,255, 0,0,0,0}};
    const yh::Image down = yh::resizeImage(edge, 1, 1);
    CHECK(down.rgba == std::vector<unsigned char>{255,80,20,128});
    const yh::Image up = yh::resizeImage(edge, 8, 4);
    CHECK(up.valid() && up.width == 8 && up.height == 4);
    for (size_t i = 0; i < up.rgba.size(); i += 4)
        if (up.rgba[i + 3] > 0) CHECK(up.rgba[i] == 255 && up.rgba[i + 1] == 80 && up.rgba[i + 2] == 20);
    CHECK(yh::resizeImage(source, 2, 2).rgba == source.rgba);
    const yh::Image wide{4, 2, {1,0,0,255, 2,0,0,255, 3,0,0,255, 4,0,0,255,
                              5,0,0,255, 6,0,0,255, 7,0,0,255, 8,0,0,255}};
    CHECK(yh::squareImage(wide, 2).rgba == std::vector<unsigned char>{2,0,0,255, 3,0,0,255, 6,0,0,255, 7,0,0,255});
    const auto fit = yh::fitImage(wide, 2);
    CHECK(fit.width == 2 && fit.height == 1 && yh::fitImage(wide, 8).rgba == wide.rgba);
    const yh::Image circle = yh::circleImage(yh::Image{1, 1, {255,80,20,128}}, 16);
    CHECK(circle.valid() && circle.rgba[3] == 0 && circle.rgba[(8 * 16 + 8) * 4 + 3] == 128);
    bool softRim = false;
    for (size_t i = 3; i < circle.rgba.size(); i += 4) softRim |= circle.rgba[i] > 0 && circle.rgba[i] < 128;
    CHECK(softRim);
    const yh::Image broken{2, 2, {1}};
    CHECK(!yh::resizeImage(broken, 4, 4).valid() && !yh::squareImage(broken, 4).valid());
    CHECK(!yh::circleImage(broken, 4).valid() && yh::sliceImage(broken, 1, 1).empty());
    CHECK(!yh::fitImage(source, 0).valid() && !yh::resizeImage(source, 0, 2).valid());
    yh::Image sheet{8, 5, std::vector<unsigned char>(8 * 5 * 4, 255)};
    sheet.rgba[(1 * 8 + 1) * 4] = 10; sheet.rgba[(1 * 8 + 4) * 4] = 20;
    const auto cells = yh::sliceImage(sheet, 2, 2, 1, 1);
    CHECK(cells.size() == 2 && cells[0].rgba[0] == 10 && cells[1].rgba[0] == 20);
    CHECK(yh::sliceImage(sheet, 2, 2, std::numeric_limits<int>::max()).empty());
    CHECK(yh::sliceImage(sheet, 2, 2, 0, std::numeric_limits<int>::max()).size() == 1);
    CHECK(yh::sliceImage(sheet, 0, 2).empty() && yh::sliceImage(sheet, 2, 2, 0, -1).empty());
}

void imageTilesets()
{
    const yh::Image source = rgbaFixture();
    const yh::Image broken{2, 2, {1}};
    // Bad tilesets fail before touching the GPU, including overflowing caller-supplied sizes.
    yh::Renderer renderer;
    std::string error;
    const std::array<yh::Image, 1> tiles{source};
    CHECK(!yh::buildTileset(renderer, {}, 64, true, &error) && !error.empty());
    CHECK(!yh::buildTileset(renderer, tiles, 0));
    CHECK(!yh::buildTileset(renderer, tiles, std::numeric_limits<int>::max()));
    CHECK(!yh::buildTileset(renderer, std::array<yh::Image, 1>{broken}));
    CHECK(!yh::buildTileset(renderer, std::array<yh::Image, 2>{source, source}, yh::maxTextureSize - 4));
    yh::Tileset atlas;
    atlas.columns = 2;
    atlas.rows = 1;
    atlas.tilePixels = 16;
    atlas.padding = 2;
    const yh::Rect uv = atlas.uv(2);
    CHECK(near(uv.x, 22.0f / 40) && near(uv.y, 2.0f / 20) && near(uv.w, 16.0f / 40) && near(uv.h, 16.0f / 20));
    CHECK(atlas.uv(0).w == 0 && atlas.uv(3).w == 0);
}

}

void images()
{
    imageFormats();
    jpegOrientations();
    imageTransforms();
    imageTilesets();
}

}
