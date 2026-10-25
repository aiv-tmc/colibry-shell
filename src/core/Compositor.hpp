// Composition root of the Colibry compositor.
//
// CCompositor owns every subsystem and the global desktop state: the
// window stack, the current workspace, the focused window and the merged
// damage region. It performs no rendering itself; frames are produced by
// the GPU renderer on demand and presented by the per-output wrappers.

#pragma once

#include "../config/ConfigManager.hpp"
#include "EventLoop.hpp"
#include "Output.hpp"

#include <cstdint>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/signal/Signal.hpp>
#include <vector>

namespace Aquamarine {
    class CBackend;
}

namespace Colibry {

    class CDisplay;
    class CSeat;
    class CCursor;
    class CWallpaper;
    class CClearRenderer;
    class CWindow;
    class CSurface;
    class CXdgShell;
    class CIPC;
    class CConfigManager;

    class CCompositor {
      public:
        static CCompositor& get();

        // Creates the config, display, seat, protocol globals and the
        // backend, starts it, initializes the GPU renderer and arms the
        // frame timers. Returns false when startup failed.
        bool init();

        // Installs signal handlers and the startup watchdog, then runs the
        // event loop until shutdown. Returns the loop exit code.
        int  run();
        void shutdown();

        static void requestExit();
        bool        isShuttingDown() const;

        // Merges a rectangle into the global damage region and schedules a
        // frame on every output.
        void addDamage(const Hyprutils::Math::CBox& box);
        void addFullDamage();

        Hyprutils::Math::CRegion takeDamage();

        void raise(CWindow* window, bool mapped);

        void focusWindow(CWindow* window);
        void focusNext();
        void focusPrev();

        void removeWindow(CWindow* window);

        Hyprutils::Math::CBox windowBox(CWindow* window) const;

        // Window content under the given point, topmost first. The
        // server-side titlebar is not part of the hit area.
        CSurface* surfaceAt(const Hyprutils::Math::Vector2D& pos) const;

        void moveWindowToWorkspace(CWindow* window, int workspace);
        void switchWorkspace(int workspace);
        void applyTiling();
        void toggleTilingMode();
        void focusOnWorkspace(int workspace);
        int  currentWorkspace() const;
        bool workspaceHasTiling() const;
        int  firstEmptyWorkspace() const;

        // Pushes the state of every window to foreign-toplevel clients.
        void refreshForeignToplevel();

        void requestRender();

        bool inputLocked() const;

        double   fadeFactor(uint64_t now) const;
        uint64_t startMs() const;
        uint64_t currentMs() const;

        // Called when an output successfully commits a frame; disarms the
        // startup watchdog on the first one.
        static void notifyFramePresented();

        // Lazy accessor for the GBM-backed buffer factory. Initializes the
        // factory from the backend DRM node on first use.
        Hyprutils::Memory::CSharedPointer<CClearRenderer> renderer();

      public:
        Hyprutils::Memory::CSharedPointer<Aquamarine::CBackend> m_backend;
        Hyprutils::Memory::CSharedPointer<CDisplay>             m_display;
        Hyprutils::Memory::CSharedPointer<CSeat>                m_seat;
        Hyprutils::Memory::CSharedPointer<CCursor>              m_cursor;
        Hyprutils::Memory::CSharedPointer<CWallpaper>           m_wallpaper;
        Hyprutils::Memory::CSharedPointer<CXdgShell>            m_xdgShell;
        Hyprutils::Memory::CSharedPointer<CIPC>                 m_ipc;

        std::vector<Hyprutils::Memory::CSharedPointer<COutput>> m_outputs;
        std::vector<Hyprutils::Memory::CSharedPointer<CWindow>> m_windows;

        CWindow* m_focusedWindow    = nullptr;
        int      m_currentWorkspace = 1;

        bool m_shutdown = false;

      private:
        CCompositor();
        ~CCompositor();

        CCompositor(const CCompositor&)            = delete;
        CCompositor& operator=(const CCompositor&) = delete;

        void onNewOutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output);

        // (Re)registers the backend poll descriptors with the event loop;
        // safe to call repeatedly (VT switches re-announce descriptors).
        void registerPollFDs();

        Hyprutils::Memory::CSharedPointer<CConfigManager> m_config;
        Hyprutils::Memory::CSharedPointer<CClearRenderer> m_renderer;

        CEventLoop m_eventLoop;

        Hyprutils::Math::CRegion m_damage;

        uint64_t m_startMs = 0;

        Hyprutils::Signal::CHyprSignalListener m_newOutputListener;
        Hyprutils::Signal::CHyprSignalListener m_newKeyboardListener;
        Hyprutils::Signal::CHyprSignalListener m_newPointerListener;
    };

}
