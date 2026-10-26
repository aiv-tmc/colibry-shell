// Implementation of the master/stack tiling layout.

#include "Layout.hpp"

#include "../core/Compositor.hpp"
#include "Window.hpp"

#include <vector>

#include <xdg-shell-server-protocol.h>

namespace ColibryShell {
    namespace Layout {

        void applyTiling() {
            auto& compositor = CCompositor::get();

            std::vector<CWindow*> tiled;
            for (const auto& window : compositor.m_windows) {
                if (window->visible() && window->isTiling() && !window->isFullscreen())
                    tiled.push_back(window.get());
            }

            if (tiled.empty())
                return;

            const auto bounds =
                compositor.m_outputs.empty() ? Hyprutils::Math::Vector2D(1920, 1080) : compositor.m_outputs.front()->size();

            static const std::vector<uint32_t> TILED_STATES = {XDG_TOPLEVEL_STATE_TILED_LEFT, XDG_TOPLEVEL_STATE_TILED_RIGHT,
                                                               XDG_TOPLEVEL_STATE_TILED_TOP, XDG_TOPLEVEL_STATE_TILED_BOTTOM};

            if (tiled.size() == 1) {
                tiled.front()->setPosition({0, 0});
                tiled.front()->configure(bounds, TILED_STATES);
                compositor.requestRender();
                return;
            }

            const double masterW = bounds.x / 2.0;
            tiled.front()->setPosition({0, 0});
            tiled.front()->configure({masterW, bounds.y}, TILED_STATES);

            const int    count = (int)tiled.size() - 1;
            const double each  = bounds.y / count;
            for (int i = 0; i < count; ++i) {
                tiled[i + 1]->setPosition({masterW, i * each});
                tiled[i + 1]->configure({bounds.x - masterW, each}, TILED_STATES);
            }

            compositor.requestRender();
        }

    }
}
