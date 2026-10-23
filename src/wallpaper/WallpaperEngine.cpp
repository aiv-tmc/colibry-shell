// Implementation of wallpaper loading and drawing.

#include "WallpaperEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <jpeglib.h>
#include <png.h>

namespace Colibry {

    // Opaque black used around a fit-scaled image.
    static constexpr uint32_t CLEAR_COLOR = 0xFF000000;

    // Detects the image format from its magic bytes.
    static bool isPng(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        unsigned char magic[4] = {0};
        file.read(reinterpret_cast<char*>(magic), 4);
        return file.gcount() == 4 && magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G';
    }

    // Decodes a PNG through the libpng simplified API into an RGBA buffer.
    static bool decodePng(const std::string& path, std::vector<uint8_t>& out, Hyprutils::Math::Vector2D& size) {
        png_image image;
        std::memset(&image, 0, sizeof(image));
        image.version = PNG_IMAGE_VERSION;

        if (png_image_begin_read_from_file(&image, path.c_str()) == 0)
            return false;

        image.format = PNG_FORMAT_RGBA;

        out.resize(PNG_IMAGE_SIZE(image));
        if (png_image_finish_read(&image, nullptr, out.data(), 0, nullptr) == 0) {
            png_image_free(&image);
            out.clear();
            return false;
        }

        size = {(int)image.width, (int)image.height};
        png_image_free(&image);
        return true;
    }

    // Decodes a JPEG through libjpeg-turbo into an RGBA buffer.
    static bool decodeJpeg(const std::string& path, std::vector<uint8_t>& out, Hyprutils::Math::Vector2D& size) {
        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
            return false;

        jpeg_decompress_struct cinfo = {};
        jpeg_error_mgr       jerr    = {};
        cinfo.err                    = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, file);

        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
            jpeg_destroy_decompress(&cinfo);
            fclose(file);
            return false;
        }

        jpeg_start_decompress(&cinfo);
        size = {(int)cinfo.output_width, (int)cinfo.output_height};

        const int          width      = size.x;
        const int          components = cinfo.output_components;
        std::vector<uint8_t> scanline(width * components);

        out.resize((size_t)width * size.y * 4);

        while (cinfo.output_scanline < cinfo.output_height) {
            JSAMPROW row = scanline.data();
            jpeg_read_scanlines(&cinfo, &row, 1);

            const int sy = cinfo.output_scanline - 1;
            for (int x = 0; x < width; ++x) {
                uint8_t* dst   = out.data() + ((size_t)sy * width + x) * 4;
                dst[0]         = scanline[x * components + 0];
                dst[1]         = scanline[x * components + 1];
                dst[2]         = scanline[x * components + 2];
                dst[3]         = 0xFF;
            }
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        fclose(file);
        return true;
    }

    void CWallpaper::load(const std::string& path) {
        m_rgba.clear();
        m_size = {0, 0};

        if (path.empty())
            return;

        bool ok = isPng(path) ? decodePng(path, m_rgba, m_size) : decodeJpeg(path, m_rgba, m_size);
        if (!ok) {
            std::fprintf(stderr, "[colibry] failed to load wallpaper '%s'\n", path.c_str());
            m_rgba.clear();
            m_size = {0, 0};
            return;
        }

        std::fprintf(stderr, "[colibry] wallpaper loaded: %dx%d\n", (int)m_size.x, (int)m_size.y);
    }

    bool CWallpaper::hasImage() const {
        return !m_rgba.empty() && m_size.x > 0 && m_size.y > 0;
    }

    Hyprutils::Math::Vector2D CWallpaper::size() const {
        return m_size;
    }

    void CWallpaper::setFit(const std::string& fit) {
        if (fit == "fit")
            m_fit = FIT_FIT;
        else
            m_fit = FIT_COVER;
    }

    void CWallpaper::setFit(eWallpaperFit fit) {
        m_fit = fit;
    }

    // Samples the image with bilinear filtering at fractional coordinates;
    // removes the blocky aliasing of downscaled wallpapers.
    static uint32_t sampleBilinear(const std::vector<uint8_t>& rgba, int imgW, int imgH, double fx, double fy) {
        fx = std::clamp(fx, 0.0, imgW - 1.001);
        fy = std::clamp(fy, 0.0, imgH - 1.001);

        const int    x0 = (int)fx;
        const int    y0 = (int)fy;
        const double tx = fx - x0;
        const double ty = fy - y0;

        const uint8_t* p00 = rgba.data() + ((size_t)y0 * imgW + x0) * 4;
        const uint8_t* p10 = p00 + 4;
        const uint8_t* p01 = p00 + (size_t)imgW * 4;
        const uint8_t* p11 = p01 + 4;

        uint8_t rgb[3];
        for (int c = 0; c < 3; ++c) {
            const double top    = p00[c] * (1.0 - tx) + p10[c] * tx;
            const double bottom = p01[c] * (1.0 - tx) + p11[c] * tx;
            rgb[c]              = (uint8_t)(top * (1.0 - ty) + bottom * ty + 0.5);
        }

        return 0xFF000000 | ((uint32_t)rgb[2] << 16) | ((uint32_t)rgb[1] << 8) | rgb[0];
    }

    void CWallpaper::draw(uint32_t* pixels, int32_t stridePixels, int32_t width, int32_t height) const {
        if (!hasImage())
            return;

        const int imgW = (int)m_size.x;
        const int imgH = (int)m_size.y;

        if (m_fit == FIT_COVER) {
            // Enlarge by the factor that makes the scaled image cover the
            // whole output; the leftover is cropped symmetrically.
            const double s     = std::max((double)width / imgW, (double)height / imgH);
            const double cropW = width / s;
            const double cropH = height / s;
            const double offX  = (imgW - cropW) / 2.0;
            const double offY  = (imgH - cropH) / 2.0;

            for (int32_t y = 0; y < height; ++y) {
                const double    fy  = offY + (y + 0.5) / s - 0.5;
                uint32_t*       dst = pixels + y * stridePixels;
                for (int32_t x = 0; x < width; ++x)
                    dst[x] = sampleBilinear(m_rgba, imgW, imgH, offX + (x + 0.5) / s - 0.5, fy);
            }
            return;
        }

        // Fit: the whole image is visible, centered on the clear color.
        const double s     = std::min((double)width / imgW, (double)height / imgH);
        const int    dispW = (int)(imgW * s);
        const int    dispH = (int)(imgH * s);
        const int    offX  = (width - dispW) / 2;
        const int    offY  = (height - dispH) / 2;

        for (int32_t y = 0; y < height; ++y) {
            uint32_t* dst = pixels + y * stridePixels;

            if (y < offY || y >= offY + dispH) {
                for (int32_t x = 0; x < width; ++x)
                    dst[x] = CLEAR_COLOR;
                continue;
            }

            const double fy = (y - offY + 0.5) / s - 0.5;
            for (int32_t x = 0; x < width; ++x) {
                if (x < offX || x >= offX + dispW) {
                    dst[x] = CLEAR_COLOR;
                    continue;
                }
                dst[x] = sampleBilinear(m_rgba, imgW, imgH, (x - offX + 0.5) / s - 0.5, fy);
            }
        }
    }

}
