// Implementation of the constant window shadow.

#include "Shadow.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Colibry {

    void CShadow::configure(bool enabled, uint32_t color, int offsetX, int offsetY, int radius, int rounding, double opacity) {
        m_enabled  = enabled;
        m_color    = color;
        m_offset   = {(double)offsetX, (double)offsetY};
        m_radius   = std::max(0, radius);
        m_rounding = std::max(0, rounding);
        m_opacity  = std::clamp(opacity, 0.0, 1.0);
    }

    bool CShadow::enabled() const {
        return m_enabled;
    }

    void CShadow::invalidate(const CWindow* key) {
        m_cache.erase(key);
    }

    void CShadow::render(const Hyprutils::Math::Vector2D& size, SCacheEntry& entry) {
        const int r = m_radius;

        entry.size = size;
        entry.fw   = (int32_t)size.x + 2 * r;
        entry.fh   = (int32_t)size.y + 2 * r;
        entry.pixels.assign((size_t)entry.fw * entry.fh, 0);

        std::vector<uint32_t> scratch((size_t)entry.fw * entry.fh, 0);

        // Silhouette of the rounded window rectangle inside the footprint.
        const int q = std::min({m_rounding, (int)size.x / 2, (int)size.y / 2});
        for (int32_t y = 0; y < entry.fh; ++y) {
            const int yl  = y - r;
            uint32_t* row = scratch.data() + (size_t)y * entry.fw;

            if (yl < 0 || yl >= (int)size.y) {
                std::memset(row, 0, (size_t)entry.fw * 4);
                continue;
            }

            int inset = 0;
            if (q > 0) {
                int yc = -1;
                if (yl < q)
                    yc = (r + q) - y;
                else if (yl >= (int)size.y - q)
                    yc = y - (r + (int)size.y - q);
                if (yc >= 0 && yc <= q) {
                    const int chord = (int)std::lround(std::sqrt((double)q * q - (double)yc * yc));
                    inset           = q - chord;
                }
            }

            const int xl = r + inset;
            const int xr = r + (int)size.x - 1 - inset;
            for (int x = 0; x < entry.fw; ++x)
                row[x] = (x >= xl && x <= xr) ? 255 : 0;
        }

        // Separable sliding-window box blur (horizontal then vertical).
        const int window = 2 * r + 1;
        std::vector<uint32_t> blurred((size_t)entry.fw * entry.fh, 0);

        for (int32_t y = 0; y < entry.fh; ++y) {
            const uint32_t* src = scratch.data() + (size_t)y * entry.fw;
            uint32_t*       dst = blurred.data() + (size_t)y * entry.fw;

            uint32_t sum = 0;
            for (int i = -r; i <= r; ++i)
                sum += src[std::clamp(i, 0, entry.fw - 1)];

            for (int x = 0; x < entry.fw; ++x) {
                dst[x] = sum / (uint32_t)window;
                sum -= src[std::clamp(x - r, 0, entry.fw - 1)];
                sum += src[std::clamp(x + r + 1, 0, entry.fw - 1)];
            }
        }

        // Vertical pass; the normalized result becomes the final ARGB
        // shadow pixel with the configured color and opacity.
        const double norm = 1.0 / (255.0 * (double)window * (double)window);
        const uint32_t cr = m_color & 0xFF;
        const uint32_t cg = (m_color >> 8) & 0xFF;
        const uint32_t cb = (m_color >> 16) & 0xFF;

        for (int x = 0; x < entry.fw; ++x) {
            uint32_t sum = 0;
            for (int i = -r; i <= r; ++i)
                sum += blurred[(size_t)std::clamp(i, 0, entry.fh - 1) * entry.fw + x];

            for (int y = 0; y < entry.fh; ++y) {
                const int a = (int)((double)sum * norm * m_opacity * 255.0);
                entry.pixels[(size_t)y * entry.fw + x] = ((uint32_t)a << 24) | (cb << 16) | (cg << 8) | cr;

                sum -= blurred[(size_t)std::clamp(y - r, 0, entry.fh - 1) * entry.fw + x];
                sum += blurred[(size_t)std::clamp(y + r + 1, 0, entry.fh - 1) * entry.fw + x];
            }
        }
    }

    void CShadow::draw(const CWindow* key, Hyprutils::Math::Vector2D pos, Hyprutils::Math::Vector2D size, const Hyprutils::Math::CRegion& damage,
                       uint32_t* pixels, int32_t stridePixels, int32_t screenWidth, int32_t screenHeight) {
        if (!m_enabled || size.x <= 0 || size.y <= 0)
            return;

        // Renders the shadow field once per window size; afterwards only
        // the clipped blend runs per frame.
        auto& entry = m_cache[key];
        if (entry.pixels.empty() || entry.size != size)
            render(size, entry);

        const int r  = m_radius;
        const int fx = (int)(pos.x + m_offset.x) - r;
        const int fy = (int)(pos.y + m_offset.y) - r;

        int      rectCount = 0;
        const auto* rects  = pixman_region32_rectangles(damage.pixman(), &rectCount);

        for (int i = 0; i < rectCount; ++i) {
            const int32_t x0 = std::max(rects[i].x1, fx);
            const int32_t y0 = std::max(rects[i].y1, fy);
            const int32_t x1 = std::min(rects[i].x2, std::min(fx + entry.fw, screenWidth));
            const int32_t y1 = std::min(rects[i].y2, std::min(fy + entry.fh, screenHeight));
            if (x0 >= x1 || y0 >= y1)
                continue;

            for (int32_t y = y0; y < y1; ++y) {
                const uint32_t* srow = entry.pixels.data() + (size_t)(y - fy) * entry.fw + (x0 - fx);
                uint32_t*       drow = pixels + y * stridePixels + x0;

                for (int32_t x = 0; x < x1 - x0; ++x) {
                    const uint32_t src = srow[x];
                    const uint32_t a   = src >> 24;
                    if (a == 0)
                        continue;
                    if (a == 0xFF)
                        drow[x] = src;
                    else {
                        const uint32_t ia = 0xFF - a;
                        const uint32_t d  = drow[x];
                        drow[x]           = 0xFF000000 | ((((src & 0x00FF00FF) * a + (d & 0x00FF00FF) * ia) >> 8) & 0x00FF00FF) |
                               ((((src & 0x0000FF00) * a + (d & 0x0000FF00) * ia) >> 8) & 0x0000FF00);
                    }
                }
            }
        }
    }

}
