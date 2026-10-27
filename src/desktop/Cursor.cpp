// Implementation of the procedural pointer sprite, motion physics and
// loss detection.

#include "Cursor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ColibryShell {

    static uint64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Classic arrow outline at 24x24 design units, hotspot at the tip.
    static const double ARROW[][2] = {
        {1, 1}, {1, 20}, {6, 15}, {9, 23}, {12, 21}, {9, 14}, {15, 14},
    };
    static constexpr int ARROW_POINTS = 7;
    static constexpr double ARROW_UNIT = 24.0;

    static bool pointInPolygon(double x, double y, double scale, double inset) {
        bool inside = false;
        for (int i = 0, j = ARROW_POINTS - 1; i < ARROW_POINTS; j = i++) {
            const double xi = ARROW[i][0] * scale + inset, yi = ARROW[i][1] * scale + inset;
            const double xj = ARROW[j][0] * scale + inset, yj = ARROW[j][1] * scale + inset;

            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
                inside = !inside;
        }
        return inside;
    }

    static uint32_t parseColor(const std::string& hex) {
        if (hex.size() == 7 && hex[0] == '#') {
            const uint32_t v = (uint32_t)std::strtoul(hex.c_str() + 1, nullptr, 16);
            return 0xFF000000 | v;
        }
        return 0xFFFFFFFF; // classic white
    }

    // Rasterizes the arrow at 4x with a per-pixel polygon test, then box-
    // filters down: crisp anti-aliased edges with no external assets.
    static std::vector<uint32_t> buildArrow(int size, uint32_t rgb) {
        const int    super  = 4;
        const int    big    = size * super;
        const double scale  = (double)big / ARROW_UNIT;
        const double inset  = 1.2 * scale;   // outline thickness in big pixels

        const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;

        std::vector<uint32_t> bigPixels((size_t)big * big, 0);
        for (int y = 0; y < big; ++y) {
            for (int x = 0; x < big; ++x) {
                const double px = x + 0.5, py = y + 0.5;

                if (pointInPolygon(px, py, scale, 0.0)) {
                    uint32_t c = 0xFF000000;
                    if (pointInPolygon(px, py, scale, inset))
                        c = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                    bigPixels[(size_t)y * big + x] = c;
                }
            }
        }

        std::vector<uint32_t> out((size_t)size * size, 0);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                uint32_t rSum = 0, gSum = 0, bSum = 0, aSum = 0;
                for (int sy = 0; sy < super; ++sy) {
                    for (int sx = 0; sx < super; ++sx) {
                        const uint32_t p = bigPixels[(size_t)(y * super + sy) * big + (x * super + sx)];
                        aSum += (p >> 24) & 0xFF;
                        rSum += (p >> 16) & 0xFF;
                        gSum += (p >> 8) & 0xFF;
                        bSum += p & 0xFF;
                    }
                }
                const int n    = super * super;
                const uint32_t a = aSum / n;
                out[(size_t)y * size + x] =
                    (a << 24) | ((rSum / n) << 16) | ((gSum / n) << 8) | (bSum / n);
            }
        }
        return out;
    }

    void CCursor::load(const std::string& colorHex, int size) {
        size = std::clamp(size, 8, 96);

        m_pixels = buildArrow(size, parseColor(colorHex));
        m_size   = {size, size};
        m_hotspot = {1.0 * size / ARROW_UNIT * 1.0, 1.0 * size / ARROW_UNIT * 1.0};
        m_hotspot = {std::max(1.0, (double)size / ARROW_UNIT), std::max(1.0, (double)size / ARROW_UNIT)};

        m_position       = m_bounds / 2.0;
        m_renderPosition = m_position;
        m_velocity       = {0, 0};
        m_tilt           = 0.0;
        m_renderValid    = false;
        m_lastMoveMs     = nowMs();
        m_samples.clear();

        std::fprintf(stderr, "[colibry-shell] cursor: procedural arrow %dx%d color='%s'\n", size, size, colorHex.empty() ? "#ffffff" : colorHex.c_str());
    }

    void CCursor::setSpeed(double speed) { m_speed = std::clamp(speed, 0.05, 3.0); }

    void CCursor::setAcceleration(double acceleration) { m_acceleration = std::clamp(acceleration, 0.0, 2.0); }

    double CCursor::effectiveScale() const {
        return std::max(m_visScale, 0.05) * (1.0 + PULSE_SCALE * m_pulse);
    }

    Hyprutils::Math::Vector2D CCursor::position() const { return m_position; }

    Hyprutils::Math::Vector2D CCursor::renderPosition() const {
        if (m_override)
            return m_overridePos;

        const double k = std::min(1.0, std::hypot(m_velocity.x, m_velocity.y) / 24.0);
        return m_renderPosition + m_velocity * (0.35 * k);
    }

    Hyprutils::Math::Vector2D CCursor::hotspot() const { return m_hotspot * m_visScale; }

    double CCursor::visibilityAlpha() const { return m_visAlpha; }

    Hyprutils::Math::CBox CCursor::damageBox() const {
        const double scale  = effectiveScale();
        const double w      = m_size.x * scale;
        const double h      = m_size.y * scale;
        const auto   anchor = renderPosition();
        const double left   = anchor.x - m_hotspot.x * scale;
        const double top    = anchor.y - m_hotspot.y * scale;

        return Hyprutils::Math::CBox({left - 12, top - 12}, {w + 24, h + 24});
    }

    Hyprutils::Math::Vector2D CCursor::clampAnchor(Hyprutils::Math::Vector2D pos) const {
        // The whole sprite must stay visible: the anchor can approach each
        // edge only as far as the sprite extends past it.
        const double scale  = std::max(m_visScale, 0.05);
        const double left   = m_hotspot.x * scale;
        const double right  = m_size.x * scale - m_hotspot.x * scale;
        const double top    = m_hotspot.y * scale;
        const double bottom = m_size.y * scale - m_hotspot.y * scale;

        pos.x = std::clamp(pos.x, left, std::max(left, m_bounds.x - 1.0 - right));
        pos.y = std::clamp(pos.y, top, std::max(top, m_bounds.y - 1.0 - bottom));

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

    void CCursor::moveBy(Hyprutils::Math::Vector2D delta) {
        // Config speed, plus a mild acceleration curve so small touchpad
        // deltas stay precise and fast flicks travel further.
        const double boost = 1.0 + m_acceleration * std::min(3.0, std::hypot(delta.x, delta.y) / 12.0);
        setPosition(m_position + delta * (m_speed * boost));
    }

    void CCursor::setRenderOverride(Hyprutils::Math::Vector2D pos) {
        m_override    = true;
        m_overridePos = pos;
    }

    void CCursor::clearRenderOverride() { m_override = false; }

    void CCursor::feedSample(uint64_t now) {
        if (!m_samples.empty() && std::hypot(m_position.x - m_samples.back().pos.x, m_position.y - m_samples.back().pos.y) < 2.0)
            return;

        m_samples.push_back({m_position, now});
        m_lastSampleMoveMs = now;

        while (!m_samples.empty() && now - m_samples.front().timeMs > LOSS_WINDOW_MS)
            m_samples.pop_front();
    }

    void CCursor::analyzeLoss(uint64_t now) {
        if (m_samples.size() < (size_t)LOSS_MIN_SAMPLES || now < m_lossCooldownUntil)
            return;

        const double diag = std::hypot(m_bounds.x, m_bounds.y);

        double path      = 0.0;
        int    reversals = 0;
        int    lastSignX = 0;

        Hyprutils::Math::Vector2D lo = m_samples.front().pos, hi = lo;

        for (size_t i = 1; i < m_samples.size(); ++i) {
            const auto d = m_samples[i].pos - m_samples[i - 1].pos;
            path += std::hypot(d.x, d.y);

            if (std::abs(d.x) > 3.0) {
                const int sign = d.x > 0 ? 1 : -1;
                if (lastSignX != 0 && sign != lastSignX)
                    ++reversals;
                lastSignX = sign;
            }

            lo.x = std::min(lo.x, m_samples[i].pos.x);
            lo.y = std::min(lo.y, m_samples[i].pos.y);
            hi.x = std::max(hi.x, m_samples[i].pos.x);
            hi.y = std::max(hi.y, m_samples[i].pos.y);
        }

        const double coverage = (hi.x - lo.x) * (hi.y - lo.y) / std::max(1.0, m_bounds.x * m_bounds.y);

        if (reversals >= LOSS_MIN_REVERSALS && path > LOSS_MIN_PATH_FRAC * diag && coverage > LOSS_MIN_COVERAGE) {
            m_pulse             = 1.0;
            m_pulseStartMs      = now;
            m_lossCooldownUntil = now + LOSS_COOLDOWN_MS;
            m_samples.clear();
        }
    }

    bool CCursor::step(uint64_t now) {
        bool animating = false;

        feedSample(now);
        analyzeLoss(now);

        if (m_pulse > 0.0) {
            const double t = std::min(1.0, (double)(now - m_pulseStartMs) / (double)PULSE_DECAY_MS);
            m_pulse        = 1.0 - t;
            animating      = true;
        }

        const bool idle = now - std::max(m_lastMoveMs, m_lastSampleMoveMs) > IDLE_HIDE_MS;

        const double hideTarget = (idle && m_pulse <= 0.0) ? 0.0 : 1.0;
        const double ease       = idle ? HIDE_EASE : SHOW_EASE;

        const double nextAlpha = m_visAlpha + (hideTarget - m_visAlpha) * ease;
        m_visAlpha             = std::abs(nextAlpha - hideTarget) < 0.01 ? hideTarget : nextAlpha;

        m_visScale = VIS_MIN_SCALE + (1.0 - VIS_MIN_SCALE) * m_visAlpha;

        if (m_visAlpha != hideTarget)
            animating = true;

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

        // Lean into horizontal motion, eased like physical weight.
        const double targetTilt = std::clamp(m_velocity.x * TILT_SPEED, -TILT_MAX_DEG, TILT_MAX_DEG);
        m_tilt                  = m_tilt + (targetTilt - m_tilt) * 0.3;
        if (std::abs(m_tilt) > 0.05)
            animating = true;

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

        const double scale = effectiveScale();
        w = std::max(1, (int)std::ceil(m_size.x * scale));
        h = std::max(1, (int)std::ceil(m_size.y * scale));

        out.assign((size_t)w * h, 0);
        static bool s_loggedRaster = false;
        if (!s_loggedRaster) {
            s_loggedRaster = true;
            std::fprintf(stderr, "[colibry-shell] cursor raster: %dx%d alpha=%.2f center=%08x\n", w, h, m_visAlpha,
                         m_pixels[(size_t)(m_size.y / 2) * (int)m_size.x + (int)(m_size.x / 2)]);
        }

        const double angle = m_tilt * 3.14159265 / 180.0;
        const double cosA  = std::cos(angle);
        const double sinA  = std::sin(angle);
        const double halfW = w / 2.0;
        const double halfH = h / 2.0;

        auto sample = [&](double sx, double sy) -> uint32_t {
            if (sx < 0.0 || sy < 0.0 || sx > m_size.x - 1.0 || sy > m_size.y - 1.0)
                return 0;

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

            const uint32_t fade = (uint32_t)(m_visAlpha * 255.0);
            return (((a * fade / 255) << 24) | ((r * fade / 255) << 16) | ((g * fade / 255) << 8) | (b * fade / 255));
        };

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const double dx = (x + 0.5) - halfW;
                const double dy = (y + 0.5) - halfH;
                const double rx = dx * cosA + dy * sinA;
                const double ry = -dx * sinA + dy * cosA;

                const double sx = (rx + halfW) / scale;
                const double sy = (ry + halfH) / scale;

                out[(size_t)y * w + x] = sample(sx, sy);
            }
        }

        return true;
    }

}
