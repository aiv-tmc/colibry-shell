// CPU text rasterization for GPU-composited decorations.
//
// TitleText renders the few strings the desktop still needs on the CPU —
// the window title with its min/max/close glyphs and the pointer sprite —
// into tightly-cropped RGBA buffers that the scene passes upload as
// textures. Everything expensive (blending, gradients, blur, rounding)
// stays on the GPU; this module only produces glyphs.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Colibry {

    class CCursor;

    namespace TitleText {

        // Loads a TrueType font. An empty path scans /usr/share/fonts for
        // the first .ttf file. Returns false when no font is usable.
        bool init(const char* path);

        // Rasterizes a full titlebar overlay: the window title on the left
        // and three background-less button glyphs on the right. hover in
        // {-1, 0, 1, 2} selects the highlighted button (min, max, close).
        void rasterizeOverlay(const std::string& title, int32_t width, int32_t height, int32_t buttonsWidth, int hover,
                              std::vector<uint32_t>& out);

        // Rasterizes a plain single-line label cropped to its ink bounds.
        void rasterizeLabel(const std::string& text, int32_t maxWidth, std::vector<uint32_t>& out, int32_t& w, int32_t& h);

        // Rasterizes the pointer sprite into a hotspot-anchored buffer,
        // applying the visibility animation scale. Returns false when the
        // sprite is fully transparent.
        bool rasterizeCursor(CCursor& cursor, std::vector<uint32_t>& out, int32_t& w, int32_t& h);

    }

}
