// Implementation of the pointer sprite, motion and visibility state.

#include "Cursor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <X11/Xcursor/Xcursor.h>

namespace ColibryShell {

    static uint64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

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
            std::fprintf(stderr, "[colibry-shell] failed to load cursor theme '%s'\n", theme.c_str());
            if (images)
                XcursorImagesDestroy(images);
            return;
        }

        XcursorImage* image = images->images[0];

        m_size    = {(int)image->width, (int)image->height};
        m_hotspot = {(int)image->xhot, (int)image->yhot};

        // Xcursor pixels are premultiplied ARGB; the GL path blends with
        // SRC_ALPHA/ONE_MINUS_SRC_ALPHA, so convert to straight alpha.
        m_pixels.resize((size_t)image->width * image->height);
        for (size_t i = 0; i < m_pixels.size(); ++i) {
            const uint32_t p = image->pixels[i];
            const uint8_t  a = (p >> 24) & 0xFF;
            if (a == 0) {
                m_pixels[i] = 0;
                continue;
            }
            const uint32_t r = ((p >> 16) & 0xFF) * 255 / a;
            const uint32_t g = ((p >> 8) & 0xFF) * 255 / a;
            const uint32_t b = (p & 0xFF) * 255 / a;
            m_pixels[i]      = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
        }

        XcursorImagesDestroy(images);

        m_position       = m_bounds / 2.0;
        m_renderPosition = m_position;
        m_velocity       = {0, 0};
        m_renderValid    = false;
        m_lastMoveMs     = nowMs();

        std::fprintf(stderr, "[colibry-shell] cursor: %s %dx%d\n", theme.c_str(), (int)m_size.x, (int)m_size.y);
    }

    void CCursor::setSpeed(double speed) { m_speed = std::clamp(speed, 0.05, 3.0); }

    Hyprutils::Math::Vector2D CCursor::position() const { return m_position; }

    Hyprutils::Math::Vector2D CCursor::renderPosition() const {
        if (m_override)
            return m_overridePos;

        // Extrapolate the smoothed position along its velocity so the
        // drawn cursor sits exactly under a fast-moving hand.
        const double k = std::min(1.0, std::hypot(m_velocity.x, m_velocity.y) / 24.0);
        return m_renderPosition + m_velocity * (EXTRAPOLATE_FRACTION * k);
    }

    Hyprutils::Math::Vector2D CCursor::hotspot() const { return m_hotspot * m_visScale; }

    double CCursor::visibilityAlpha() const { return m_visAlpha; }

    Hyprutils::Math::CBox CCursor::damageBox() const {
        const double scale  = std::max(m_visScale, 0.05);
        const double w      = m_size.x * scale;
        const double h      = m_size.y * scale;
        const auto   anchor = renderPosition();
        const double left   = anchor.x - m_hotspot.x * scale;
        const double top    = anchor.y - m_hotspot.y * scale;

        return Hyprutils::Math::CBox({left - 3, top - 3}, {w + 6, h + 6});
    }

    Hyprutils::Math::Vector2D CCursor::clampAnchor(Hyprutils::Math::Vector2D pos) const {
        pos.x = std::clamp(pos.x, 0.0, std::max(0.0, m_bounds.x - 1.0));
        pos.y = std::clamp(pos.y, 0.0, std::max(0.0, m_bounds.y - 1.0));

        // Rounded screen corners are forbidden territory: the anchor
        // slides along the quarter-circle arc like along the edges.
        if (m_cornerRadius > 0.0) {
            const double r = m_cornerRadius;
            struct SCorner {
                double cx, cy, sx, sy;
            };
            const SCorner corners[4] = {
                {r, r, -1.0, -1.0},
                {m_bounds.x - 1.0 - r, r, 1.0, -1.0},
                {r, m_bounds.y - 1.0 - r, -1.0, 1.0},
                {m_bounds.x - 1.0 - r, m_bounds.y - 1.0 - r, 1.0, 1.0},
            };

            for (const auto& c : corners) {
                const double dx = pos.x - c.cx;
                const double dy = pos.y - c.cy;
                if (dx * c.sx > 0.0 && dy * c.sy > 0.0) {
                    const double dist = std::hypot(dx, dy);
                    if (dist < r) {
                        const double t = r / std::max(dist, 1e-6);
                        pos.x          = c.cx + dx * t;
                        pos.y          = c.cy + dy * t;
                    }
                }
            }
        }

        return pos;
    }

    void CCursor::setBounds(Hyprutils::Math::Vector2D bounds) {
        m_bounds = bounds;
        setPosition(m_position);
    }

    void CCursor::setCornerRadius(double radius) {
        m_cornerRadius = std::max(0.0, radius);
        setPosition(m_position);
    }

    void CCursor::setPosition(Hyprutils::Math::Vector2D pos) {
        m_position    = clampAnchor(pos);
        m_lastMoveMs  = nowMs();
        m_renderValid = false;
    }

    void CCursor::moveBy(Hyprutils::Math::Vector2D delta) { setPosition(m_position + delta * m_speed); }

    void CCursor::setRenderOverride(Hyprutils::Math::Vector2D pos) {
        m_override    = true;
        m_overridePos = pos;
    }

    void CCursor::clearRenderOverride() { m_override = false; }

    bool CCursor::step(uint64_t now) {
        bool animating = false;

        // Idle detection: the visibility animation eases toward hidden
        // after IDLE_HIDE_MS of stillness and back on any activity.
        const bool idle = now - m_lastMoveMs > IDLE_HIDE_MS;

        const double hideTarget = idle ? 0.0 : 1.0;
        const double ease       = idle ? HIDE_EASE : SHOW_EASE;

        const double nextAlpha = m_visAlpha + (hideTarget - m_visAlpha) * ease;
        m_visAlpha             = std::abs(nextAlpha - hideTarget) < 0.01 ? hideTarget : nextAlpha;

        m_visScale = VIS_MIN_SCALE + (1.0 - VIS_MIN_SCALE) * m_visAlpha;

        if (m_visAlpha != hideTarget)
            animating = true;

        // The rendered position chases the anchor; the ease rises while
        // moving so fast flicks stay crisp and rest settles gently.
        const auto target = m_override ? m_overridePos : m_position;
        const bool moving = std::abs(target.x - m_renderPosition.x) > 0.05 || std::abs(target.y - m_renderPosition.y) > 0.05;

        if (moving || !m_renderValid) {
            const double k = moving ? MOVE_EASE : REST_EASE;
            const auto   next = m_renderPosition + (target - m_renderPosition) * k;
            m_velocity        = next - m_renderPosition;
            const bool arrived = std::abs(next.x - target.x) < 0.05 && std::abs(next.y - target.y) < 0.05;
            m_renderPosition  = arrived ? target : next;
            animating         = animating || !arrived;
            m_renderValid     = true;
        } else
            m_velocity = m_velocity * 0.8;

        return animating;
    }

    void CCursor::follow() {
        m_renderPosition = m_position;
        m_velocity       = {0, 0};
        m_renderValid    = true;
    }

    void CCursor::snapRender() {
        m_renderPosition = m_override ? m_overridePos : m_position;
        m_velocity       = {0, 0};
        m_renderValid    = true;
    }

    bool CCursor::rasterize(std::vector<uint32_t>& out, int32_t& w, int32_t& h) const {
        if (m_pixels.empty() || m_visAlpha <= 0.01)
            return false;

        const double scale = std::max(m_visScale, 0.05);
        w = std::max(1, (int)std::ceil(m_size.x * scale));
        h = std::max(1, (int)std::ceil(m_size.y * scale));

        out.assign((size_t)w * h, 0);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const double sx = std::min((double)m_size.x - 1.0, x / scale);
                const double sy = std::min((double)m_size.y - 1.0, y / scale);

                const int x0 = (int)sx, y0 = (int)sy;
                const int x1 = std::min((int)m_size.x - 1, x0 + 1);
                const int y1 = std::min((int)m_size.y - 1, y0 + 1);
                const double fx = sx - x0, fy = sy - y0;

                const uint32_t c00 = m_pixels[(size_t)y0 * (int)m_size.x + x0];
                const uint32_t c10 = m_pixels[(size_t)y0 * (int)m_size.x + x1];
                const uint32_t c01 = m_pixels[(size_t)y1 * (int)m_size.x + x0];
                const uint32_t c11 = m_pixels[(size_t)y1 * (int)m_size.x + x1];

                uint32_t r = 0, g = 0, b = 0, a = 0;
                for (int ch = 0; ch < 4; ++ch) {
                    const double v0 = ((c00 >> (ch * 8)) & 0xFF) * (1.0 - fx) + ((c10 >> (ch * 8)) & 0xFF) * fx;
                    const double v1 = ((c01 >> (ch * 8)) & 0xFF) * (1.0 - fx) + ((c11 >> (ch * 8)) & 0xFF) * fx;
                    const double v  = v0 * (1.0 - fy) + v1 * fy;
                    switch (ch) {
                        case 0: a = (uint32_t)v; break;
                        case 1: b = (uint32_t)v; break;
                        case 2: g = (uint32_t)v; break;
                        default: r = (uint32_t)v; break;
                    }
                }

                out[(size_t)y * w + x] = ((a * (uint32_t)(m_visAlpha * 255.0) / 255) << 24) | (r << 16) | (g << 8) | b;
            }
        }

        return true;
    }

}
