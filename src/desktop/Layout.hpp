// Tiling layout for the Colibry compositor.
//
// Layout::applyTiling recomputes the master/stack arrangement of the
// tiled, visible, non-fullscreen windows of the current workspace: the
// first tiled window masters the left half, the rest share the right
// half vertically.

#pragma once

namespace ColibryShell {
    namespace Layout {

        // Recomputes positions and configures the sizes of tiled windows on
        // the current workspace.
        void applyTiling();

    }
}
