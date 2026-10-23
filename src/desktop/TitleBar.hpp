// Server-side title bar for Colibry windows.
//
// The title bar is drawn by the compositor above the window content: a
// colored strip with the application icon and title on the left and the
// minimize, maximize/restore and close buttons on the right, in the Windows
// layout. Buttons show hover and press states. The title text is rendered
// with FreeType from a configured or auto-detected font; without a font the
// title is omitted and the vector glyphs still draw the buttons. Bar height
// and button width are configured at startup.

#pragma once

#include <cstdint>
#include <string>

#include <hyprutils/math/Vector2D.hpp>

namespace Colibry {

    class CWindow;

    namespace TitleBar {
        // Applies the title bar configuration (bar height in pixels, button
        // width in pixels); called once before any drawing.
        void configure(int height, int buttonWidth);

        // Bar height in pixels.
        int height();
        // Window button width in pixels.
        int buttonWidth();

        // Initializes the title font; an empty configured path selects the
        // first available system font.
        void initFont(const std::string& configuredPath);

        // Draws a single line of text with the title font at the given top
        // y, truncated to maxWidth; no-op when no font is loaded.
        void drawText(uint32_t* pixels, int32_t stridePixels, int32_t screenWidth, int32_t screenHeight, int32_t x, int32_t y, const char* str,
                      int32_t maxWidth, uint32_t color);

        // Hit test results for a position inside a window rectangle.
        enum eHit {
            HIT_NONE = 0,
            HIT_DRAG, // title bar strip or transparent content
            HIT_MINIMIZE,
            HIT_MAXIMIZE,
            HIT_CLOSE,
            // Resize hits carry an edge bitmask starting at bit 8.
            HIT_RESIZE_BASE = 0x100,
        };

        // Maps a position inside the window's full rectangle (title bar
        // included) to a hit result.
        uint32_t hitTest(Hyprutils::Math::Vector2D local, Hyprutils::Math::Vector2D contentSize);

        // Draws the title bar of the given window into an XRGB8888 surface.
        void draw(const CWindow* window, bool focused, int hoverButton, bool leftDown, uint32_t* pixels, int32_t stridePixels, int32_t screenWidth,
                  int32_t screenHeight);
    }

}
