// Application launcher overlay for the Colibry compositor.
//
// CLauncher is the model behind the macOS-style launcher: it scans
// .desktop files, fuzzy-filters them by the typed filter, tracks the
// selection and launches the chosen entry. Rendering lives in the GPU
// layer (LauncherDraw); input arrives through CSeat while the overlay is
// active.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Colibry {

    struct SLauncherEntry {
        std::string name;
        std::string exec;
        std::string icon;
    };

    class CLauncher {
      public:
        static CLauncher& get();

        // Scans the standard desktop-file directories. Returns false when
        // nothing usable was found.
        bool init();

        bool active() const;
        void open();
        void close();
        void toggle();

        // Keyboard input routed by the seat while the overlay is active.
        void onKey(uint32_t sym, uint32_t mods, bool pressed);

        const std::string&              filter() const;
        const std::vector<SLauncherEntry>& entries() const;
        int                             selected() const;

      private:
        CLauncher()  = default;
        ~CLauncher() = default;

        void rescan();
        void refilter();
        void launch(const SLauncherEntry& entry);

        bool                      m_active   = false;
        std::string               m_filter;
        std::vector<SLauncherEntry> m_apps;
        std::vector<int>          m_filtered;
        int                       m_selected = 0;
    };

}
