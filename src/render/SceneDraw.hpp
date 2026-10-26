// Scene passes for the GPU compositor.
//
// SceneDraw renders the regular desktop content: window shadows (GPU-blurred
// silhouettes cached per size), window content with signed-distance rounded
// corners, the adaptive gradient titlebar with its text overlay, and the
// pointer sprite with its visibility animation.

#pragma once

#include <cstdint>

namespace ColibryShell {

    class CGLEngine;

    namespace SceneDraw {

        // Loads the titlebar font (empty path picks the first system font).
        bool initTitleFont(const char* path);

        // Draws shadows, window contents and titlebars in stacking order.
        void drawWindows(CGLEngine& e, int32_t screenW, int32_t screenH);

        // Draws the pointer sprite at its smoothed position.
        void drawCursor(CGLEngine& e, int32_t screenW, int32_t screenH);

    }

}
