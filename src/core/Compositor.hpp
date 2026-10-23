// Central coordinator of the Colibry compositor process.
//
// CCompositor owns the Aquamarine backend, the event loop, all live outputs
// and windows, the workspace state, the wallpaper, the shadow, the cursor,
// the Wayland display, the seat, xdg-shell and the IPC server. main() only
// calls init(), run() and shutdown() on the singleton instance.

#pragma once

#include "../desktop/Cursor.hpp"
#include "../desktop/Window.hpp"
#include "../protocol/Display.hpp"
#include "../protocol/Seat.hpp"
#include "../protocol/XdgShell.hpp"
#include "../render/SolidRenderer.hpp"
#include "../wallpaper/WallpaperEngine.hpp"

#include "EventLoop.hpp"
#include "Output.hpp"

#include <aquamarine/backend/Backend.hpp>

#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/signal/Signal.hpp>

#include <csignal>
#include <vector>

namespace Colibry {

    class CConfigManager;
    class CIPC;
    class CShadow;

    class CCompositor {
      public:
        static CCompositor& get();

        bool init();
        int  run();
        void shutdown();

        static void requestExit();
        static void notifyFramePresented();

        void onNewOutput(Hyprutils::Memory::CSharedPointer<Aquamarine::IOutput> output);

        // Topmost visible window surface (client content) at the position.
        CSurface* surfaceAt(Hyprutils::Math::Vector2D pos);

        // Raises the window and moves keyboard focus to it.
        void focusWindow(CWindow* window);
        void focusSurface(CSurface* surface);
        void focusNext();
        void focusPrev();
        void removeWindow(CWindow* window);

        int  currentWorkspace() const;
        void switchWorkspace(int workspace);
        void moveWindowToWorkspace(CWindow* window, int workspace);
        bool workspaceHasTiling(int workspace) const;
        int  firstEmptyWorkspace() const;
        void applyTiling();
        void refreshForeignToplevel();

        // Damage accumulation for the damage-driven renderer: addDamage
        // dirties a rectangle, addFullDamage forces every output to redraw
        // completely, takeDamage returns the accumulated region and clears
        // it.
        void addDamage(const Hyprutils::Math::CBox& box);
        void addFullDamage();
        Hyprutils::Math::CRegion takeDamage();

        // The full outer rectangle of the window (content plus title bar).
        Hyprutils::Math::CBox windowBox(const CWindow* window) const;

        void requestRender();

        Hyprutils::Memory::CSharedPointer<CClearRenderer> renderer();

        std::vector<Hyprutils::Memory::CSharedPointer<COutput>> m_outputs;
        std::vector<Hyprutils::Memory::CSharedPointer<CWindow>> m_windows;

        CWindow* m_focusedWindow = nullptr;

        Hyprutils::Memory::CSharedPointer<Aquamarine::CBackend> m_backend;
        Hyprutils::Memory::CSharedPointer<CDisplay>             m_display;
        Hyprutils::Memory::CSharedPointer<CSeat>                m_seat;
        Hyprutils::Memory::CSharedPointer<CXdgShell>            m_xdgShell;
        Hyprutils::Memory::CSharedPointer<CWallpaper>           m_wallpaper;
        Hyprutils::Memory::CSharedPointer<CCursor>              m_cursor;
        Hyprutils::Memory::CSharedPointer<CShadow>              m_shadow;

        CEventLoop m_eventLoop;

        Hyprutils::Memory::CSharedPointer<CConfigManager> m_config;
        Hyprutils::Memory::CSharedPointer<CIPC>           m_ipc;

        Hyprutils::Signal::CHyprSignalListener m_newOutputListener;
        Hyprutils::Signal::CHyprSignalListener m_newKeyboardListener;
        Hyprutils::Signal::CHyprSignalListener m_newPointerListener;

      private:
        CCompositor()          = default;
        ~CCompositor()         = default;

        CCompositor(const CCompositor&)            = delete;
        CCompositor& operator=(const CCompositor&) = delete;

        std::vector<Aquamarine::SBackendImplementationOptions> backendOptions() const;
        void                                                   registerPollFDs();

        int m_currentWorkspace = 1;

        Hyprutils::Math::CRegion m_damage;

        Hyprutils::Memory::CSharedPointer<CClearRenderer> m_renderer;
    };

}
