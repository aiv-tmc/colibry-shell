// Implementation of the server-side title bar.

#include "TitleBar.hpp"

#include "Window.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace Colibry {
    namespace TitleBar {

        static int g_height  = 32;
        static int g_buttonW = 46;

        static constexpr uint32_t COL_ACTIVE_BG      = 0xFF2B2B2B;
        static constexpr uint32_t COL_INACTIVE_BG    = 0xFF3B3B3B;
        static constexpr uint32_t COL_TITLE          = 0xFFFFFFFF;
        static constexpr uint32_t COL_TITLE_DIM      = 0xFFAAAAAA;
        static constexpr uint32_t COL_BUTTON         = 0xFFE6E6E6;
        static constexpr uint32_t COL_HOVER          = 0xFF505050;
        static constexpr uint32_t COL_HOVER_INACTIVE = 0xFF4A4A4A;
        static constexpr uint32_t COL_HOVER_CLOSE    = 0xFFE81123;
        static constexpr uint32_t COL_PRESS          = 0xFF3A3A3A;
        static constexpr uint32_t COL_PRESS_CLOSE    = 0xFFC50F1F;

        static FT_Library g_ftLibrary = nullptr;
        static FT_Face    g_ftFace    = nullptr;

        static const char* FONT_CANDIDATES[] = {
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        };

        void configure(int height, int buttonWidth) {
            g_height  = std::max(16, height);
            g_buttonW = std::max(24, buttonWidth);
        }

        int height() {
            return g_height;
        }

        int buttonWidth() {
            return g_buttonW;
        }

        void initFont(const std::string& configuredPath) {
            if (FT_Init_FreeType(&g_ftLibrary) != 0) {
                std::fprintf(stderr, "[colibry] titlebar: FreeType unavailable\n");
                return;
            }

            std::string path = configuredPath;
            if (path.empty()) {
                for (const auto* candidate : FONT_CANDIDATES) {
                    if (access(candidate, R_OK) == 0) {
                        path = candidate;
                        break;
                    }
                }
            }

            if (path.empty()) {
                std::fprintf(stderr, "[colibry] titlebar: no font found; set titlebar_font in colibry.toml\n");
                return;
            }

            if (FT_New_Face(g_ftLibrary, path.c_str(), 0, &g_ftFace) != 0) {
                std::fprintf(stderr, "[colibry] titlebar: failed to load font '%s'\n", path.c_str());
                g_ftFace = nullptr;
                return;
            }

            FT_Set_Pixel_Sizes(g_ftFace, 0, g_height * 45 / 100);
            std::fprintf(stderr, "[colibry] titlebar: font '%s'\n", path.c_str());
        }

        static void put(uint32_t* pixels, int32_t stride, int32_t w, int32_t h, int32_t x, int32_t y, uint32_t color) {
            if (x >= 0 && y >= 0 && x < w && y < h)
                pixels[y * stride + x] = color;
        }

        static void rect(uint32_t* pixels, int32_t stride, int32_t w, int32_t h, int32_t x, int32_t y, int32_t rw, int32_t rh, uint32_t color) {
            for (int32_t yy = y; yy < y + rh; ++yy)
                for (int32_t xx = x; xx < x + rw; ++xx)
                    put(pixels, stride, w, h, xx, yy, color);
        }

        static void blend(uint32_t* pixels, int32_t stride, int32_t w, int32_t h, int32_t x, int32_t y, uint32_t color, int alpha) {
            if (x < 0 || y < 0 || x >= w || y >= h || alpha <= 0)
                return;
            if (alpha >= 255) {
                put(pixels, stride, w, h, x, y, color);
                return;
            }

            const int    ia  = 255 - alpha;
            uint32_t*    dst = pixels + y * stride + x;
            const uint32_t d = *dst;
            *dst             = 0xFF000000 | ((((color & 0xFF) * alpha + (d & 0xFF) * ia) / 255) & 0xFF) |
                   ((((((color >> 8) & 0xFF) * alpha + ((d >> 8) & 0xFF) * ia) / 255) & 0xFF) << 8) |
                   ((((((color >> 16) & 0xFF) * alpha + ((d >> 16) & 0xFF) * ia) / 255) & 0xFF) << 16);
        }

        static void line(uint32_t* pixels, int32_t stride, int32_t w, int32_t h, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
            const int32_t steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
            for (int32_t i = 0; i <= steps; ++i)
                put(pixels, stride, w, h, x0 + (x1 - x0) * i / (steps ? steps : 1), y0 + (y1 - y0) * i / (steps ? steps : 1), color);
        }

        static void glyphBitmap(uint32_t* pixels, int32_t stride, int32_t w, int32_t h, int32_t x0, int32_t y0, const FT_Bitmap& bitmap,
                                uint32_t color) {
            for (uint32_t r = 0; r < bitmap.rows; ++r) {
                for (uint32_t c = 0; c < bitmap.width; ++c) {
                    const int alpha = bitmap.buffer[r * bitmap.pitch + c];
                    blend(pixels, stride, w, h, x0 + (int32_t)c, y0 + (int32_t)r, color, alpha);
                }
            }
        }

        static void text(uint32_t* pixels, int32_t stride, int32_t w, int32_t h, int32_t x, int32_t y, const char* str, int32_t maxWidth,
                         uint32_t color) {
            if (!g_ftFace)
                return;

            const int baseline = y + g_height / 2 + (int)(g_ftFace->size->metrics.ascender >> 6) / 2;

            int32_t cx = x;
            for (const char* p = str; *p && cx < x + maxWidth - g_height / 2; ++p) {
                if (FT_Load_Char(g_ftFace, *p, FT_LOAD_RENDER) != 0)
                    continue;

                const FT_GlyphSlot glyph = g_ftFace->glyph;
                glyphBitmap(pixels, stride, w, h, cx + glyph->bitmap_left, baseline - glyph->bitmap_top, glyph->bitmap, color);
                cx += (int32_t)(glyph->advance.x >> 6);
            }
        }

        void drawText(uint32_t* pixels, int32_t stridePixels, int32_t screenWidth, int32_t screenHeight, int32_t x, int32_t y, const char* str,
                      int32_t maxWidth, uint32_t color) {
            text(pixels, stridePixels, screenWidth, screenHeight, x, y, str, maxWidth, color);
        }

        static uint32_t hashOf(const std::string& str) {
            uint32_t hash = 2166136261u;
            for (const char c : str)
                hash = (hash ^ (unsigned char)c) * 16777619u;
            return hash;
        }

        static uint32_t avatarColor(uint32_t hash) {
            const int    hue    = (int)(hash % 360);
            const double s      = 0.55;
            const double v      = 0.75;
            const int    sector = hue / 60;
            const double f      = (double)(hue % 60) / 60.0;
            const double p      = v * (1.0 - s);
            const double q      = v * (1.0 - s * f);
            const double t      = v * (1.0 - s * (1.0 - f));

            double r = 0, g = 0, b = 0;
            switch (sector) {
                case 0: r = v; g = t; b = p; break;
                case 1: r = q; g = v; b = p; break;
                case 2: r = p; g = v; b = t; break;
                case 3: r = p; g = q; b = v; break;
                case 4: r = t; g = p; b = v; break;
                default: r = v; g = p; b = q; break;
            }

            return 0xFF000000 | ((uint32_t)(b * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(r * 255);
        }

        static void icon(const CWindow* window, uint32_t* pixels, int32_t stride, int32_t w, int32_t h, int32_t x, int32_t y) {
            const int    size = g_height - 14;
            const uint32_t bg = avatarColor(hashOf(!window->appId().empty() ? window->appId() : window->title()));

            for (int32_t yy = 0; yy < size; ++yy) {
                for (int32_t xx = 0; xx < size; ++xx) {
                    const int q  = size / 4;
                    const int dx = xx < q ? q - xx : (xx >= size - q ? xx - (size - q - 1) : 0);
                    const int dy = yy < q ? q - yy : (yy >= size - q ? yy - (size - q - 1) : 0);
                    if (dx > 0 && dy > 0 && dx * dx + dy * dy > q * q)
                        continue;
                    put(pixels, stride, w, h, x + xx, y + yy, bg);
                }
            }

            char letter = !window->title().empty() ? window->title().front() : '?';
            if (letter >= 'a' && letter <= 'z')
                letter -= 32;
            if (g_ftFace) {
                const int savedSize = g_ftFace->size->metrics.x_ppem;
                FT_Set_Pixel_Sizes(g_ftFace, 0, size - 4);
                if (FT_Load_Char(g_ftFace, letter, FT_LOAD_RENDER) == 0) {
                    const FT_GlyphSlot glyph = g_ftFace->glyph;
                    glyphBitmap(pixels, stride, w, h, x + (size - (int)glyph->bitmap.width) / 2, y + (size - (int)glyph->bitmap.rows) / 2,
                                glyph->bitmap, 0xFFFFFFFF);
                }
                FT_Set_Pixel_Sizes(g_ftFace, 0, savedSize);
            }
        }

        static int32_t buttonX(int32_t barWidth, int32_t index) {
            return barWidth - g_buttonW * (3 - index);
        }

        static void drawButtonGlyph(uint32_t* pixels, int32_t stride, int32_t w, int32_t h, int32_t x, int32_t y, int32_t index, bool maximized,
                                    uint32_t color) {
            const int32_t cx = x + g_buttonW / 2;
            const int32_t cy = y + g_height / 2;

            switch (index) {
                case 0:
                    line(pixels, stride, w, h, cx - 5, cy + 3, cx + 5, cy + 3, color);
                    break;
                case 1:
                    line(pixels, stride, w, h, cx - 5, cy - 4, cx + 5, cy - 4, color);
                    line(pixels, stride, w, h, cx - 5, cy + 5, cx + 5, cy + 5, color);
                    line(pixels, stride, w, h, cx - 5, cy - 4, cx - 5, cy + 5, color);
                    line(pixels, stride, w, h, cx + 5, cy - 4, cx + 5, cy + 5, color);
                    if (maximized) {
                        line(pixels, stride, w, h, cx - 2, cy - 6, cx + 7, cy - 6, color);
                        line(pixels, stride, w, h, cx + 7, cy - 6, cx + 7, cy + 3, color);
                        line(pixels, stride, w, h, cx - 2, cy - 6, cx - 2, cy - 3, color);
                    }
                    break;
                case 2:
                    line(pixels, stride, w, h, cx - 5, cy - 5, cx + 5, cy + 5, color);
                    line(pixels, stride, w, h, cx - 5, cy + 5, cx + 5, cy - 5, color);
                    break;
            }
        }

        uint32_t hitTest(Hyprutils::Math::Vector2D local, Hyprutils::Math::Vector2D contentSize) {
            const int32_t x = (int32_t)local.x;
            const int32_t y = (int32_t)local.y;
            const int32_t w = (int32_t)contentSize.x;
            const int32_t h = (int32_t)(contentSize.y + g_height);

            if (x < 0 || y < 0 || x >= w || y >= h)
                return HIT_NONE;

            const int32_t border = 4;
            uint32_t     edges   = 0;
            if (x < border)
                edges |= 0x100;
            if (x >= w - border)
                edges |= 0x200;
            if (y >= h - border)
                edges |= 0x800;
            if (edges)
                return HIT_RESIZE_BASE | edges;

            if (y < g_height) {
                if (x >= buttonX(w, 2) && x < buttonX(w, 2) + g_buttonW)
                    return HIT_CLOSE;
                if (x >= buttonX(w, 1) && x < buttonX(w, 1) + g_buttonW)
                    return HIT_MAXIMIZE;
                if (x >= buttonX(w, 0) && x < buttonX(w, 0) + g_buttonW)
                    return HIT_MINIMIZE;
                return HIT_DRAG;
            }

            return HIT_DRAG;
        }

        void draw(const CWindow* window, bool focused, int hoverButton, bool leftDown, uint32_t* pixels, int32_t stridePixels, int32_t screenWidth,
                  int32_t screenHeight) {
            if (!window)
                return;

            const auto pos  = window->position();
            const auto size = window->size();
            const int  x    = (int32_t)pos.x;
            const int  y    = (int32_t)pos.y;
            const int  w    = (int32_t)size.x;

            if (x >= screenWidth || y >= screenHeight || x + w <= 0 || y + g_height <= 0)
                return;

            const uint32_t bg = focused ? COL_ACTIVE_BG : COL_INACTIVE_BG;

            rect(pixels, stridePixels, screenWidth, screenHeight, x, y, w, g_height, bg);

            icon(window, pixels, stridePixels, screenWidth, screenHeight, x + 8, y + 7);
            text(pixels, stridePixels, screenWidth, screenHeight, x + g_height - 2, y, window->title().c_str(), w - 3 * g_buttonW - g_height,
                 focused ? COL_TITLE : COL_TITLE_DIM);

            for (int32_t i = 0; i < 3; ++i) {
                const int32_t bx = x + buttonX(w, i);

                if (hoverButton == i) {
                    const bool pressed = leftDown;
                    uint32_t   fill    = 0;
                    if (i == 2)
                        fill = pressed ? COL_PRESS_CLOSE : COL_HOVER_CLOSE;
                    else
                        fill = pressed ? COL_PRESS : (focused ? COL_HOVER : COL_HOVER_INACTIVE);
                    rect(pixels, stridePixels, screenWidth, screenHeight, bx, y, g_buttonW, g_height, fill);
                }

                const int32_t offset = (hoverButton == i && leftDown) ? 1 : 0;
                drawButtonGlyph(pixels, stridePixels, screenWidth, screenHeight, bx, y + offset, i, window->maximized(), COL_BUTTON);
            }
        }

    }
}
