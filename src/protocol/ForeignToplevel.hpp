// Foreign toplevel management for the Colibry compositor.
//
// CForeignToplevelManager implements zwlr_foreign_toplevel_manager_v1 so
// taskbars and docks can list the open windows, watch their titles, app
// ids, states and output visibility, and request actions (activate,
// minimize, maximize, fullscreen, close). Each client binding the manager
// receives its own handle resource per window.

#pragma once

#include <map>
#include <vector>
#include <wayland-server.h>

namespace Colibry {

    class CDisplay;
    class CWindow;

    class CForeignToplevelManager {
      public:
        static CForeignToplevelManager& get();

        // Creates the zwlr_foreign_toplevel_manager_v1 global.
        bool init(CDisplay* display);

        // Window lifecycle hooks called by the desktop layer.
        void onWindowMapped(CWindow* window);
        void onWindowUnmapped(CWindow* window);
        void onWindowTitle(CWindow* window);
        void onWindowAppId(CWindow* window);
        // Recomputes the state bits and output visibility of the window and
        // notifies every client.
        void onWindowState(CWindow* window);

      private:
        CForeignToplevelManager()  = default;
        ~CForeignToplevelManager() = default;

        CForeignToplevelManager(const CForeignToplevelManager&)            = delete;
        CForeignToplevelManager& operator=(const CForeignToplevelManager&) = delete;

        // One handle resource handed to a single client for one window.
        struct SHandle {
            wl_resource* resource;
            bool         entered = false; // output_enter already sent
        };

        // One client binding of the manager.
        struct SClient {
            wl_resource*                resource;
            bool                        stopped = false;
            std::map<CWindow*, SHandle> handles;
        };

        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);
        static void stop(wl_client* client, wl_resource* resource);

        static void handleSetMaximized(wl_client* client, wl_resource* resource);
        static void handleUnsetMaximized(wl_client* client, wl_resource* resource);
        static void handleSetMinimized(wl_client* client, wl_resource* resource);
        static void handleUnsetMinimized(wl_client* client, wl_resource* resource);
        static void handleActivate(wl_client* client, wl_resource* resource, wl_resource* seat);
        static void handleClose(wl_client* client, wl_resource* resource);
        static void handleSetRectangle(wl_client* client, wl_resource* resource, wl_resource* surface, int32_t x, int32_t y, int32_t width,
                                       int32_t height);
        static void handleSetFullscreen(wl_client* client, wl_resource* resource, wl_resource* output);
        static void handleUnsetFullscreen(wl_client* client, wl_resource* resource);
        static void handleDestroy(wl_client* client, wl_resource* resource);

        // Maps a handle resource back to its window.
        static CWindow* windowFor(wl_resource* handle);

        // Creates a handle for the window on the client and announces it.
        void createHandle(SClient* client, CWindow* window);
        // Sends state and output visibility for the window to one client.
        void updateHandle(SClient* client, CWindow* window);

        CDisplay* m_display = nullptr;

        wl_global* m_global = nullptr;

        std::vector<SClient*> m_clients;
    };

}
