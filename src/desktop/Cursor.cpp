// Implementation of cursor loading, smoothing and rendering.

#include "Cursor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <X11/Xcursor/Xcursor.h>

namespace Colibry {

    // Time constant of the exponential smoothing in milliseconds: lower
    // values track the real position faster, higher values are smoother.
    static constexpr double SMOOTHING_TAU_MS = 30.0;

    // Jumps larger than this many pixels snap instantly instead of
    // gliding (warps, clamping at screen edges).
    static constexpr double SNAP_DISTANCE = 60.0;

    void CCursor::load(const std::string& themeOverride, int sizeOverride) {
        std::string theme = themeOverride;
        if (theme.empty()) {
            if (const char* env = std::getenv("XCURSOR_THEME"))
                theme = env;
        }
        if (theme.empty())
            theme = "default";

        int size = sizeOverride;
        if (size <= 0) {
            if (const char* env = std::getenv("XCURSOR_SIZE"))
                size = std::atoi(env);
        }
        if (size <= 0)
            size = 24;

        XcursorImages* images = XcursorLibraryLoadImages("left_ptr", theme.c_str(), size);
        if (!images || images->nimage < 1) {
            std::fprintf(stderr, "[colibry] failed to load cursor theme '%s'\n", theme.c_str());
            if (images)
                XcursorImagesDestroy(images);
            return;
        }

        XcursorImage* image = images->images[0];

        m_size    = {(int)image->width, (int)image->height};
        m_hotspot = {(int)image->xhot, (int)image->yhot};
        m_pixels.assign(image->pixels, image->pixels + (int64_t)image->width * image->height);

        XcursorImagesDestroy(images);
        std::fprintf(stderr, "[colibry] cursor loaded: theme '%s' %dx%d\n", theme.c_str(), (int)m_size.x, (int)m_size.y);
    }

    Hyprutils::Math::Vector2D CCursor::position() const {
        return m_position;
    }

    Hyprutils::Math::Vector2D CCursor::renderPosition() const {
        return m_renderPosition;
    }

    Hyprutils::Math::CBox CCursor::damageBox() const {
        return {m_renderPosition.x - m_hotspot.x - 1, m_renderPosition.y - m_hotspot.y - 1, m_size.x + 2, m_size.y + 2};
    }

    void CCursor::updateRenderPosition(uint32_t timeMs) {
        if (!m_renderValid) {
            snapRender();
            m_lastRenderUpdate = timeMs;
            return;
        }

        const Hyprutils::Math::Vector2D delta = m_position - m_renderPosition;

        // Large jumps (teleports) render immediately.
        if (std::abs(delta.x) > SNAP_DISTANCE || std::abs(delta.y) > SNAP_DISTANCE) {
            snapRender();
            m_lastRenderUpdate = timeMs;
            return;
        }

        const double dt = std::min<uint32_t>(timeMs - m_lastRenderUpdate, 100);
        m_lastRenderUpdate = timeMs;

        // Exponential smoothing: the drawn position closes a fraction of
        // the remaining distance, proportional to the elapsed time.
        const double k = 1.0 - std::exp(-dt / SMOOTHING_TAU_MS);
        m_renderPosition += delta * k;

        // Close enough: settle exactly to avoid endless sub-pixel drift.
        if (std::abs(m_position.x - m_renderPosition.x) < 0.05 && std::abs(m_position.y - m_renderPosition.y) < 0.05)
            m_renderPosition = m_position;
    }

    void CCursor::snapRender() {
        m_renderPosition = m_position;
        m_renderValid    = true;
    }

    void CCursor::setPosition(Hyprutils::Math::Vector2D pos) {
        m_position.x = std::clamp(pos.x, 0.0, std::max(0.0, m_bounds.x - 1.0));
        m_position.y = std::clamp(pos.y, 0.0, std::max(0.0, m_bounds.y - 1.0));
    }

    void CCursor::moveBy(Hyprutils::Math::Vector2D delta) {
        setPosition({m_position.x + delta.x, m_position.y + delta.y});
    }

    void CCursor::setBounds(Hyprutils::Math::Vector2D bounds) {
        m_bounds = bounds;
        setPosition(m_position);
    }

    void CCursor::draw(uint32_t* pixels, int32_t stridePixels, int32_t width, int32_t height) const {
        if (m_pixels.empty())
            return;

        const int32_t x0 = (int32_t)m_renderPosition.x - (int32_t)m_hotspot.x;
        const int32_t y0 = (int32_t)m_renderPosition.y - (int32_t)m_hotspot.y;

        for (int32_t y = 0; y < (int32_t)m_size.y; ++y) {
            const int32_t dy = y0 + y;
            if (dy < 0 || dy >= height)
                continue;

            for (int32_t x = 0; x < (int32_t)m_size.x; ++x) {
                const int32_t dx = x0 + x;
                if (dx < 0 || dx >= width)
                    continue;

                // XCursor pixels are premultiplied ARGB.
                const uint32_t src = m_pixels[y * (int32_t)m_size.x + x];
                const uint32_t a   = src >> 24;
                if (a == 0)
                    continue;

                uint32_t* dst = pixels + dy * stridePixels + dx;
                if (a == 0xFF)
                    *dst = src | 0xFF000000;
                else {
                    const uint32_t ia = 0xFF - a;
                    const uint32_t d  = *dst;
                    *dst              = 0xFF000000 | ((src & 0x00FF00FF) + ((((d & 0x00FF00FF) * ia) >> 8) & 0x00FF00FF)) |
                           ((((src & 0x0000FF00) + ((((d & 0x0000FF00) * ia) >> 8) & 0x0000FF00)) & 0x0000FF00));
                }
            }
        }
    }

}
