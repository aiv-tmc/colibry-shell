// Cursor state and rendering for the Colibry compositor.
//
// CCursor loads the left_ptr image from the XCursor theme given at load()
// (falling back to XCURSOR_THEME and "default") at the configured size and
// tracks the pointer position. The drawn position is exponentially
// smoothed toward the real position (time constant 30 ms) so batches of
// input events render as continuous motion instead of steps; warps and the
// first frame snap instantly.

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

        // Loads the cursor image. An empty theme selects XCURSOR_THEME or
        // "default"; a non-positive size selects XCURSOR_SIZE or 24.
        void load(const std::string& theme, int size);

        // Real pointer position on screen (input target).
        Hyprutils::Math::Vector2D position() const;
        // Drawn pointer position after smoothing; all rendering and damage
        // use this.
        Hyprutils::Math::Vector2D renderPosition() const;
        // The rectangle the drawn cursor covers plus a one-pixel margin;
        // used for damage tracking.
        Hyprutils::Math::CBox damageBox() const;

        // Places the pointer at the given position, clamped to the bounds.
        void setPosition(Hyprutils::Math::Vector2D pos);
        // Moves the pointer by a relative delta, clamped to the screen.
        void moveBy(Hyprutils::Math::Vector2D delta);
        // Sets the clamping bounds from the output size.
        void setBounds(Hyprutils::Math::Vector2D bounds);

        // Advances the drawn position toward the real position with
        // exponential smoothing; time is the input event timestamp in
        // milliseconds. Large jumps snap instantly.
        void updateRenderPosition(uint32_t timeMs);
        // Instantly aligns the drawn position with the real position
        // (warps, first frame).
        void snapRender();

        // Blends the cursor image into an XRGB8888 surface at the drawn
        // position.
        void draw(uint32_t* pixels, int32_t stridePixels, int32_t width, int32_t height) const;

      private:
        std::vector<uint32_t>     m_pixels; // premultiplied ARGB
        Hyprutils::Math::Vector2D m_size;
        Hyprutils::Math::Vector2D m_hotspot;
        Hyprutils::Math::Vector2D m_position;
        Hyprutils::Math::Vector2D m_bounds = {1920, 1080};

        // Smoothed render position state.
        Hyprutils::Math::Vector2D m_renderPosition;
        uint32_t                  m_lastRenderUpdate = 0;
        bool                      m_renderValid      = false;
    };

}
