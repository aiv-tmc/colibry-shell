// Window switcher overlay for the Colibry compositor.
//
// CSwitcher opens on SUPER+Tab and lists the windows of the current
// workspace (minimized windows included, so the switcher can restore
// them). SUPER+Tab cycles the selection, Enter activates the selected
// window, Escape cancels; any other key cancels the overlay and is
// processed normally. The overlay renders on top of the desktop: a dimmed
// screen with a centered panel of window titles.

#pragma once

#include <cstdint>
#include <vector>

namespace Colibry {

    class CWindow;

    class CSwitcher {
      public:
        static CSwitcher& get();

        // True while the overlay is visible.
        bool active() const;

        // Opens the overlay with the windows of the current workspace,
        // starting at the window after the focused one.
        void open();
        // Moves the selection by the given direction (+1/-1).
        void cycle(int direction);
        // Focuses and raises the selected window (restoring it when
        // minimized) and closes the overlay.
        void activate();
        // Closes the overlay without changing focus.
        void cancel();

        // Draws the overlay into an XRGB8888 surface.
        void draw(uint32_t* pixels, int32_t stridePixels, int32_t screenWidth, int32_t screenHeight) const;

      private:
        CSwitcher()  = default;
        ~CSwitcher() = default;

        bool m_active = false;
        int  m_index  = 0;

        // Snapshot of candidate windows taken at open(); entries may die
        // while the overlay is up and are validated on every use.
        std::vector<CWindow*> m_entries;
    };

}
