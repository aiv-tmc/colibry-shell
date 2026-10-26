// xdg-shell implementation for the Colibry compositor.
//
// CXdgShell owns the xdg_wm_base global. xdg_surface objects track the
// protocol configure/ack handshake and xdg_toplevel objects create desktop
// windows. Positioners store real placement state for the popup stage.

#pragma once

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <wayland-server.h>

namespace ColibryShell {

    class CDisplay;
    class CSurface;

    class CXdgShell {
      public:
        // Creates the xdg_wm_base global on the display.
        bool init(CDisplay* display);

        // Sends the initial xdg_toplevel/xdg_surface configure pair to a
        // toplevel window unless it was already sent.
        static void sendInitialConfigure(wl_resource* xdgSurfaceResource);

      private:
        // xdg_wm_base bind callback.
        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);
        static void destroy(wl_client* client, wl_resource* resource);
        static void createPositioner(wl_client* client, wl_resource* resource, uint32_t id);
        static void getXdgSurface(wl_client* client, wl_resource* resource, uint32_t id, wl_resource* surfaceResource);
        static void pong(wl_client* client, wl_resource* resource, uint32_t serial);

        // xdg_surface requests.
        static void surfaceDestroy(wl_client* client, wl_resource* resource);
        static void getToplevel(wl_client* client, wl_resource* resource, uint32_t id);
        static void getPopup(wl_client* client, wl_resource* resource, uint32_t id, wl_resource* parent, wl_resource* positioner);
        static void setWindowGeometry(wl_client* client, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h);
        static void ackConfigure(wl_client* client, wl_resource* resource, uint32_t serial);

        // xdg_toplevel requests.
        static void toplevelDestroy(wl_client* client, wl_resource* resource);
        static void setParent(wl_client* client, wl_resource* resource, wl_resource* parent);
        static void setTitle(wl_client* client, wl_resource* resource, const char* title);
        static void setAppId(wl_client* client, wl_resource* resource, const char* appId);
        static void showWindowMenu(wl_client* client, wl_resource* resource, wl_resource* seat, uint32_t serial, int32_t x, int32_t y);
        static void move(wl_client* client, wl_resource* resource, wl_resource* seat, uint32_t serial);
        static void resize(wl_client* client, wl_resource* resource, wl_resource* seat, uint32_t serial, uint32_t edges);
        static void setMaxSize(wl_client* client, wl_resource* resource, int32_t w, int32_t h);
        static void setMinSize(wl_client* client, wl_resource* resource, int32_t w, int32_t h);
        static void setMaximized(wl_client* client, wl_resource* resource);
        static void unsetMaximized(wl_client* client, wl_resource* resource);
        static void setFullscreen(wl_client* client, wl_resource* resource, wl_resource* output);
        static void unsetFullscreen(wl_client* client, wl_resource* resource);
        static void setMinimized(wl_client* client, wl_resource* resource);

        CDisplay*  m_display = nullptr;
        wl_global* m_global  = nullptr;
    };

}
