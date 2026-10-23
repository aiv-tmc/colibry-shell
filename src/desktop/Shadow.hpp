// Constant realistic window shadow for the Colibry compositor.
//
// CShadow renders a soft drop shadow of a window rectangle: the rounded
// rectangle silhouette is blurred with two separable sliding-window box
// blur passes. The rendered shadow is cached per window and reused until
// the window size changes, so per-frame cost is a clipped blend of the
// cached pixels.

#pragma once

#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <cstdint>
#include <map>
#include <vector>

namespace Colibry {

    class CWindow;

    class CShadow {
      public:
        CShadow()          = default;
        ~CShadow()         = default;

        // Sets all shadow parameters at once.
        void configure(bool enabled, uint32_t color, int offsetX, int offsetY, int radius, int rounding, double opacity);

        // True when the shadow should be drawn.
        bool enabled() const;

        // Draws the shadow of the given window (content size plus title
        // bar) into an XRGB8888 surface, clipped to the damage region. The
        // key identifies the cache entry and is invalidated when the window
        // is destroyed.
        void draw(const CWindow* key, Hyprutils::Math::Vector2D pos, Hyprutils::Math::Vector2D size, const Hyprutils::Math::CRegion& damage,
                  uint32_t* pixels, int32_t stridePixels, int32_t screenWidth, int32_t screenHeight);

        // Drops the cached shadow of a destroyed window.
        void invalidate(const CWindow* key);

      private:
        // One rendered shadow field, keyed by the window.
        struct SCacheEntry {
            Hyprutils::Math::Vector2D size;
            int32_t                   fw = 0;
            int32_t                   fh = 0;
            std::vector<uint32_t>     pixels; // ARGB, alpha from the blur
        };

        // Renders the shadow field for the given size into the cache entry.
        void render(const Hyprutils::Math::Vector2D& size, SCacheEntry& entry);

        bool                      m_enabled  = true;
        uint32_t                  m_color    = 0xFF000000;
        Hyprutils::Math::Vector2D m_offset   = {0, 12};
        int                       m_radius   = 24;
        int                       m_rounding = 8;
        double                    m_opacity  = 0.45;

        std::map<const CWindow*, SCacheEntry> m_cache;
    };

}
