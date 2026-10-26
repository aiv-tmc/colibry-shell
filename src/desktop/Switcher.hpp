// Window switcher model for the Colibry compositor.
//
// CSwitcher tracks the overlay state: the candidate windows of the current
// workspace, the selection index and open/close transitions. It holds no
// rendering code; the GPU pass in SwitcherDraw reads the model through
// entries() and selected() and rasterizes the overlay.

#pragma once

#include <cstdint>
#include <vector>

namespace ColibryShell {

    class CWindow;

    class CSwitcher {
      public:
        static CSwitcher& get();

        bool active() const;

        // Opens the overlay starting at the window after the focused one.
        void open();
        // Moves the selection by the given direction (+1 or -1).
        void cycle(int direction);
        // Focuses and raises the selected window (restoring it when
        // minimized) and closes the overlay.
        void activate();
        // Closes the overlay without changing focus.
        void cancel();

        // Candidate windows, in stacking order.
        const std::vector<CWindow*>& entries() const;
        int                          selected() const;

      private:
        CSwitcher()  = default;
        ~CSwitcher() = default;

        bool m_active = false;
        int  m_index  = 0;

        std::vector<CWindow*> m_entries;
    };

}
