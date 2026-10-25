// Pointer sprite, motion and visibility state for the Colibry compositor.
//
// The anchor follows the raw input clamped to the visible screen (hard
// walls at the edges and along the rounded corner arcs); the rendered
// position chases the anchor with exponential smoothing and a short
// velocity extrapolation, so fast flicks land exactly under the hand
// without any rubber-band feel. After a few seconds of stillness the
// sprite shrinks and fades; movement brings it back with the same curve
// in reverse.

#pragma once

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Colibry {

    class CCursor {
      public:
        CCursor()          = default;
        ~CCursor()         = default;

        // Loads the left_ptr sprite from the given XCursor theme.
        void load(const std::string& theme, int size);
        void setSpeed(double speed);

        Hyprutils::Math::Vector2D position() const;
        Hyprutils::Math::Vector2D renderPosition() const;
        Hyprutils::Math::CBox     damageBox() const;

        // Hotspot in sprite pixels, scaled by the visibility animation.
        Hyprutils::Math::Vector2D hotspot() const;

        // Current visibility animation alpha (1 = fully visible).
        double visibilityAlpha() const;

        // Walls: the anchor stays inside the screen rectangle and out of
        // the rounded corners.
        void setPosition(Hyprutils::Math::Vector2D pos);
        void moveBy(Hyprutils::Math::Vector2D delta);
        void setBounds(Hyprutils::Math::Vector2D bounds);
        void setCornerRadius(double radius);

        // Pins the rendered sprite to a fixed point while a window grab is
        // holding it (the rubber band), releasing it back after.
        void setRenderOverride(Hyprutils::Math::Vector2D pos);
        void clearRenderOverride();

        // Advances smoothing (with velocity extrapolation) and the
        // visibility animation; returns true while anything is moving.
        bool step(uint64_t nowMs);

        void follow();
        void snapRender();

        // Rasterizes the sprite into a tightly cropped RGBA buffer with
        // the scaled hotspot at the origin. Returns false when invisible.
        bool rasterize(std::vector<uint32_t>& out, int32_t& w, int32_t& h) const;

      private:
        static constexpr double MOVE_EASE = 0.24;
        static constexpr double REST_EASE = 0.18;

        static constexpr uint64_t IDLE_HIDE_MS  = 3000;
        static constexpr double   VIS_MIN_SCALE = 0.55;
        static constexpr double   HIDE_EASE     = 0.14;
        static constexpr double   SHOW_EASE     = 0.34;

        // Hand-feel: fraction of a frame the smoothed position is
        // extrapolated by, so flicks don't lag behind the wrist.
        static constexpr double EXTRAPOLATE_FRACTION = 0.45;
        static constexpr double EXTRAPOLATE_MAX_PX   = 3.0;

        Hyprutils::Math::Vector2D clampAnchor(Hyprutils::Math::Vector2D pos) const;

        std::vector<uint32_t>     m_pixels;
        Hyprutils::Math::Vector2D m_size;
        Hyprutils::Math::Vector2D m_hotspot;
        Hyprutils::Math::Vector2D m_bounds = {1920, 1080};
        double                    m_cornerRadius = 0.0;
        double                    m_speed        = 1.0;

        Hyprutils::Math::Vector2D m_position;
        Hyprutils::Math::Vector2D m_renderPosition;
        Hyprutils::Math::Vector2D m_velocity;
        uint64_t                  m_lastMoveMs  = 0;
        bool                      m_renderValid = false;

        bool                      m_override = false;
        Hyprutils::Math::Vector2D m_overridePos;

        double m_visScale = 1.0;
        double m_visAlpha = 1.0;
    };

}
