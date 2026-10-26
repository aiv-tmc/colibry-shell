// Implementation of CPU text and sprite rasterization.

#include "TitleText.hpp"

#include "../desktop/Cursor.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace ColibryShell {
    namespace TitleText {

        namespace {

            FT_Library s_library = nullptr;
            FT_Face    s_face    = nullptr;
            int32_t    s_glyphH  = 16;

            // Blends one premultiplied-ish RGBA glyph pixel into the buffer.
            void blendPixel(std::vector<uint32_t>& buf, int32_t w, int32_t h, int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
                if (x < 0 || y < 0 || x >= w || y >= h || a == 0)
                    return;

                uint32_t& dst = buf[y * w + x];
                const uint8_t da = (dst >> 24) & 0xFF;
                const uint8_t dr = (dst >> 16) & 0xFF;
                const uint8_t dg = (dst >> 8) & 0xFF;
                const uint8_t db = dst & 0xFF;

                const float sa = a / 255.0f;
                const float ia = 1.0f - sa;
                dst = (std::min(255, (int)(a + da * ia)) << 24) | (std::min(255, (int)(r + dr * ia)) << 16) |
                      (std::min(255, (int)(g + dg * ia)) << 8) | std::min(255, (int)(b + db * ia));
            }

            // Draws one text line left-aligned at (x, yBaseline) in white.
            void drawText(std::vector<uint32_t>& buf, int32_t w, int32_t h, const std::string& text, int32_t x, int32_t yBaseline, int32_t maxRight) {
                if (!s_face)
                    return;

                FT_Set_Pixel_Sizes(s_face, 0, s_glyphH);

                int32_t penX = x;
                for (const char c : text) {
                    if (penX > maxRight)
                        break;
                    if (FT_Load_Char(s_face, c, FT_LOAD_RENDER) != 0)
                        continue;

                    const FT_GlyphSlot g = s_face->glyph;
                    for (int row = 0; row < (int)g->bitmap.rows; ++row) {
                        for (int col = 0; col < (int)g->bitmap.width; ++col) {
                            const uint8_t a = g->bitmap.buffer[row * g->bitmap.pitch + col];
                            blendPixel(buf, w, h, penX + g->bitmap_left + col, yBaseline - g->bitmap_top + row, 255, 255, 255, a);
                        }
                    }
                    penX += g->advance.x >> 6;
                }
            }

            // Draws a thick anti-aliased line segment in white.
            void stroke(std::vector<uint32_t>& buf, int32_t w, int32_t h, float x0, float y0, float x1, float y1, float thick) {
                const int minX = std::max(0, (int)std::floor(std::min(x0, x1) - thick));
                const int maxX = std::min(w - 1, (int)std::ceil(std::max(x0, x1) + thick));
                const int minY = std::max(0, (int)std::floor(std::min(y0, y1) - thick));
                const int maxY = std::min(h - 1, (int)std::ceil(std::max(y0, y1) + thick));

                const float dx = x1 - x0, dy = y1 - y0;
                const float len2 = dx * dx + dy * dy + 1e-6f;

                for (int y = minY; y <= maxY; ++y) {
                    for (int x = minX; x <= maxX; ++x) {
                        float t = ((x + 0.5f - x0) * dx + (y + 0.5f - y0) * dy) / len2;
                        t = std::clamp(t, 0.0f, 1.0f);
                        const float px = x0 + t * dx, py = y0 + t * dy;
                        const float dist = std::hypot(x + 0.5f - px, y + 0.5f - py);
                        const int a = (int)std::clamp((thick - dist) * 255.0f, 0.0f, 255.0f);
                        if (a > 0)
                            blendPixel(buf, w, h, x, y, 255, 255, 255, (uint8_t)a);
                    }
                }
            }

            // Draws one titlebar button glyph without any background:
            // 0 = minimize (dash), 1 = maximize (rectangle), 2 = close (cross).
            void drawButtonGlyph(std::vector<uint32_t>& buf, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t half, int kind) {
                const float t = 1.6f;
                if (kind == 0) {
                    stroke(buf, w, h, (float)(cx - half), (float)cy, (float)(cx + half), (float)cy, t);
                } else if (kind == 1) {
                    stroke(buf, w, h, (float)(cx - half), (float)(cy - half), (float)(cx + half), (float)(cy - half), t);
                    stroke(buf, w, h, (float)(cx + half), (float)(cy - half), (float)(cx + half), (float)(cy + half), t);
                    stroke(buf, w, h, (float)(cx + half), (float)(cy + half), (float)(cx - half), (float)(cy + half), t);
                    stroke(buf, w, h, (float)(cx - half), (float)(cy + half), (float)(cx - half), (float)(cy - half), t);
                } else {
                    stroke(buf, w, h, (float)(cx - half), (float)(cy - half), (float)(cx + half), (float)(cy + half), t);
                    stroke(buf, w, h, (float)(cx + half), (float)(cy - half), (float)(cx - half), (float)(cy + half), t);
                }
            }

        }

        bool init(const char* path) {
            if (FT_Init_FreeType(&s_library) != 0)
                return false;

            std::string fontPath = path ? path : "";
            if (fontPath.empty()) {
                for (const auto& root : {"/usr/share/fonts", "/usr/local/share/fonts", "~/.fonts"}) {
                    std::error_code ec;
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
                        if (entry.path().extension() == ".ttf") {
                            fontPath = entry.path().string();
                            break;
                        }
                    }
                    if (!fontPath.empty())
                        break;
                }
            }

            if (fontPath.empty() || FT_New_Face(s_library, fontPath.c_str(), 0, &s_face) != 0) {
                std::fprintf(stderr, "[colibry-shell] no titlebar font found\n");
                return false;
            }

            std::fprintf(stderr, "[colibry-shell] titlebar font: %s\n", fontPath.c_str());
            return true;
        }

        void rasterizeOverlay(const std::string& title, int32_t width, int32_t height, int32_t buttonsWidth, int hover,
                              std::vector<uint32_t>& out) {
            out.assign((size_t)width * height, 0);
            if (!s_face)
                return;

            s_glyphH = std::max(10, height * 11 / 16);

            const int32_t textLeft = 12;
            const int32_t maxRight = width - buttonsWidth - 8;
            drawText(out, width, height, title, textLeft, height / 2 + s_glyphH * 3 / 8, maxRight);

            const int32_t btnW    = buttonsWidth / 3;
            const int32_t centerY = height / 2;
            const int32_t half    = std::min(height, btnW) / 6;
            for (int i = 0; i < 3; ++i) {
                const int32_t cx = width - buttonsWidth + btnW * i + btnW / 2;
                drawButtonGlyph(out, width, height, cx, centerY, half, i);
            }
            (void)hover;
        }

        void rasterizeLabel(const std::string& text, int32_t maxWidth, std::vector<uint32_t>& out, int32_t& w, int32_t& h) {
            s_glyphH = 20;
            w = std::min(maxWidth, std::max(8, (int32_t)text.size() * s_glyphH / 2 + 8));
            h = s_glyphH + 10;
            out.assign((size_t)w * h, 0);
            drawText(out, w, h, text, 4, h / 2 + s_glyphH * 3 / 8, w - 4);
        }

        bool rasterizeCursor(CCursor& cursor, std::vector<uint32_t>& out, int32_t& w, int32_t& h) {
            if (!cursor.rasterize(out, w, h))
                return false;
            return cursor.visibilityAlpha() > 0.01f;
        }

    }
}
