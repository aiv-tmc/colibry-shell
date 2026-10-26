// Wayland display core of the Colibry compositor.
//
// CDisplay wraps wl_display, owns the protocol globals (wl_compositor and
// wl_shm) and drives the wl_event_loop together with the main poll loop.
// Clients connect through the socket created with WL_DISPLAY naming in
// XDG_RUNTIME_DIR.

#pragma once

#include <wayland-server.h>

#include <hyprutils/memory/SharedPtr.hpp>

#include <cstdint>
#include <string>
#include <vector>

// The running compositor needs the display file descriptor for its poll
// loop. Some wayland-server.h revisions shipped by distributions omit this
// declaration while the symbol exists in every released libwayland-server,
// so it is declared here explicitly with C linkage.
extern "C" int wl_display_get_fd(struct wl_display* display);

namespace ColibryShell {

    class CSurface;

    class CDisplay {
      public:
        CDisplay()          = default;
        ~CDisplay()         = default;

        // Creates wl_display, the socket and all globals. Returns false on
        // fatal failure.
        bool init();

        // Closes the socket and destroys the display with all client state.
        void destroy();

        // Returns the pollable display file descriptor.
        int fd() const;

        // Name of the created socket (for example "wayland-0"), exported to
        // clients through WAYLAND_DISPLAY.
        const std::string& socketName() const;

        // Dispatches pending wayland events and flushes outgoing buffers.
        void dispatch();

        // Sends 'done' to every pending frame callback of every surface and
        // destroys those callback resources.
        void sendFrameDone(uint32_t timeMs);

        // Registers a surface created through wl_compositor.
        void registerSurface(Hyprutils::Memory::CSharedPointer<CSurface> surface);

        // Removes a destroyed surface from the registry list.
        void unregisterSurface(CSurface* surface);

        // All live surfaces in creation order.
        const std::vector<Hyprutils::Memory::CSharedPointer<CSurface>>& surfaces() const;

        // The underlying wl_display.
        wl_display* waylandDisplay() const;

      private:
        // wl_compositor bind callback: creates surface/region resources.
        static void bindCompositor(wl_client* client, void* data, uint32_t version, uint32_t id);

        wl_display* m_display = nullptr;

        wl_global* m_compositorGlobal = nullptr;
        wl_global* m_shmGlobal        = nullptr;

        std::string m_socketName;

        std::vector<Hyprutils::Memory::CSharedPointer<CSurface>> m_surfaces;
    };

}
