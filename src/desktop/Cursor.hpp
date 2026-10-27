// Pointer sprite, motion physics and loss detection for the ColibryShell
// compositor.
//
// The sprite is procedural: a classic arrow with a dark outline, rendered
// at 4x and box-filtered down, in a config-chosen color. No theme files
// are involved, so size, color and opacity are exact.
//
// Motion is physical: exponential smoothing with velocity extrapolation,
// speed and acceleration from the configuration, and a tilt proportional
// to horizontal velocity eased like weight. The whole sprite (not just
// its anchor) is kept inside the screen.
//
// Cursor loss is detected from behaviour: searching users sweep long
// erratic paths with frequent direction reversals; a sliding window of
// samples scores that pattern and triggers a brief reveal pulse. Fast
// purposeful movement does not trigger it.

#pragma once

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ColibryShell {

    class CCursor {
      public:
        CCursor()          = default;
        ~CCursor()         = default;

        // colorHex is "#RRGGBB" or empty for the classic white arrow.
        void load(const std::string& colorHex, int size);
        void setSpeed(double speed);
        void setAcceleration(double acceleration);

        Hyprutils::Math::Vector2D position() const;
        Hyprutils::Math::Vector2D renderPosition() const;
        Hyprutils::Math::CBox     damageBox() const;

        Hyprutils::Math::Vector2D hotspot() const;
        double                    visibilityAlpha() const;

        void setPosition(Hyprutils::Math::Vector2D pos);
        void moveBy(Hyprutils::Math::Vector2D delta);
        void setBounds(Hyprutils::Math::Vector2D bounds);
        void setCornerRadius(double radius);

        void setRenderOverride(Hyprutils::Math::Vector2D pos);
        void clearRenderOverride();

        bool step(uint64_t nowMs);

        void follow();
        void snapRender();

        bool rasterize(std::vector<uint32_t>& out, int32_t& w, int32_t& h) const;

      private:
        struct SSample {
            Hyprutils::Math::Vector2D pos;
            uint64_t                  timeMs;
        };

        static constexpr double MOVE_EASE = 0.26;
        static constexpr double REST_EASE = 0.18;

        static constexpr uint64_t IDLE_HIDE_MS  = 3000;
        static constexpr double   VIS_MIN_SCALE = 0.55;
        static constexpr double   HIDE_EASE     = 0.14;
        static constexpr double   SHOW_EASE     = 0.34;

        static constexpr double EXTRAPOLATE_FRACTION = 0.35;

        static constexpr double TILT_MAX_DEG = 12.0;
        static constexpr double TILT_SPEED   = 0.8;

        static constexpr uint64_t LOSS_WINDOW_MS      = 1200;
        static constexpr int      LOSS_MIN_SAMPLES    = 8;
        static constexpr int      LOSS_MIN_REVERSALS  = 5;
        static constexpr double   LOSS_MIN_PATH_FRAC  = 0.5;
        static constexpr double   LOSS_MIN_COVERAGE   = 0.25;
        static constexpr uint64_t LOSS_COOLDOWN_MS    = 4000;
        static constexpr double   PULSE_SCALE         = 0.7;
        static constexpr uint64_t PULSE_DECAY_MS      = 700;

        Hyprutils::Math::Vector2D clampAnchor(Hyprutils::Math::Vector2D pos) const;
        void                      feedSample(uint64_t nowMs);
        void                      analyzeLoss(uint64_t nowMs);
        double                    effectiveScale() const;

        std::vector<uint32_t>     m_pixels;
        Hyprutils::Math::Vector2D m_size;
        Hyprutils::Math::Vector2D m_hotspot;
        Hyprutils::Math::Vector2D m_bounds = {1920, 1080};
        double                    m_cornerRadius = 0.0;
        double                    m_speed        = 1.0;
        double                    m_acceleration = 0.4;

        Hyprutils::Math::Vector2D m_position;
        Hyprutils::Math::Vector2D m_renderPosition;
        Hyprutils::Math::Vector2D m_velocity;
        double                    m_tilt = 0.0;
        uint64_t                  m_lastMoveMs  = 0;
        bool                      m_renderValid = false;

        bool                      m_override = false;
        Hyprutils::Math::Vector2D m_overridePos;

        double m_visScale = 1.0;
        double m_visAlpha = 1.0;

        std::deque<SSample> m_samples;
        uint64_t            m_lastSampleMoveMs  = 0;
        double              m_pulse             = 0.0;
        uint64_t            m_pulseStartMs      = 0;
        uint64_t            m_lossCooldownUntil = 0;
    };

}
