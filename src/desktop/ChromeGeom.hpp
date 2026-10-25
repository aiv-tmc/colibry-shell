// Window chrome geometry and hit-testing for the Colibry compositor.
//
// ChromeGeom is the single source of truth for everything the interactive
// layer and the renderer must agree on: the titlebar height, the button
// column geometry and the mapping from a window-local point to a semantic
// hit (drag the titlebar, press a button, grab a resize edge). All values
// come from the live configuration, so geometry changes apply instantly.

#pragma once

#include "../config/ConfigManager.hpp"

#include <hyprutils/math/Vector2D.hpp>

#include <algorithm>
#include <cstdint>

namespace Colibry {
    namespace ChromeGeom {

        // Semantic hit codes. Button codes are plain values; resize hits
        // carry edge bits in the mask HIT_RESIZE_EDGES.
        enum eHit {
            HIT_NONE   = 0,
            HIT_DRAG   = 1,
            HIT_CLOSE  = 2,
            HIT_MINIMIZE = 3,
            HIT_MAXIMIZE = 4,

            HIT_RESIZE_BASE    = 0x1000,
            HIT_EDGE_LEFT      = 0x0100,
            HIT_EDGE_RIGHT     = 0x0200,
            HIT_EDGE_TOP       = 0x0400,
            HIT_EDGE_BOTTOM    = 0x0800,
            HIT_RESIZE_EDGES   = 0x0F00,
        };

        // Server-side titlebar height in pixels.
        inline int32_t height() {
            return CConfigManager::get().titlebarHeight();
        }

        // Combined width of the three titlebar buttons.
        inline int32_t buttonsWidth() {
            return CConfigManager::get().titlebarButtonWidth() * 3;
        }

        // Maps a window-local point (origin at the window's top-left,
        // titlebar included) to a semantic hit for the given window size.
        inline int32_t hitTest(const Hyprutils::Math::Vector2D& local, const Hyprutils::Math::Vector2D& size) {
            const int32_t barH = height();
            const int32_t btnW = CConfigManager::get().titlebarButtonWidth();

            if (local.y >= 0 && local.y < barH && local.x >= 0 && local.x <= size.x) {
                if (local.x >= size.x - btnW * 3) {
                    const int32_t inColumn = (int32_t)local.x - (size.x - btnW * 3);
                    const int32_t index    = inColumn / btnW;
                    if (index == 0)
                        return HIT_MINIMIZE;
                    if (index == 1)
                        return HIT_MAXIMIZE;
                    return HIT_CLOSE;
                }
                return HIT_DRAG;
            }

            // Resize edges: an 8px grab zone around the window content.
            constexpr double BORDER = 8.0;
            int32_t hit = HIT_RESIZE_BASE;

            if (local.x < -BORDER || local.x > size.x + BORDER || local.y < barH - BORDER || local.y > size.y + barH + BORDER)
                return HIT_NONE;

            if (local.x < BORDER)
                hit |= HIT_EDGE_LEFT;
            else if (local.x > size.x - BORDER)
                hit |= HIT_EDGE_RIGHT;

            if (local.y < barH + BORDER)
                hit |= HIT_EDGE_TOP;
            else if (local.y > size.y + barH - BORDER)
                hit |= HIT_EDGE_BOTTOM;

            if (hit == HIT_RESIZE_BASE)
                return HIT_NONE;

            return hit;
        }

        // Index of the titlebar button under a window-local point, or -1.
        inline int hoveredButton(const Hyprutils::Math::Vector2D& local, const Hyprutils::Math::Vector2D& size) {
            const int32_t barH = height();
            const int32_t btnW = CConfigManager::get().titlebarButtonWidth();

            if (local.y < 0 || local.y >= barH || local.x < size.x - btnW * 3 || local.x > size.x)
                return -1;

            const int32_t index = ((int32_t)local.x - (size.x - btnW * 3)) / btnW;
            return std::clamp(index, 0, 2);
        }

    }
}
