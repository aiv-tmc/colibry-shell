// Implementation of xdg-shell.

#include "XdgShell.hpp"

#include "Display.hpp"
#include "Surface.hpp"

#include "../core/Compositor.hpp"
#include "../desktop/Window.hpp"

#include <cstdio>
#include <cstring>

#include <xdg-shell-server-protocol.h>

namespace Colibry {

    struct SXdgSurface {
        CSurface*    surface          = nullptr;
        wl_resource* resource         = nullptr;
        wl_resource* toplevelResource = nullptr;

        Hyprutils::Memory::CSharedPointer<class CWindow> window;

        uint32_t configureSerial      = 0;
        bool     initialConfigureSent = false;
    };

    struct SPositioner {
        Hyprutils::Math::Vector2D size;
        Hyprutils::Math::CBox     anchorRect;
        uint32_t                  anchor               = 0;
        uint32_t                  gravity              = 0;
        uint32_t                  constraintAdjustment = 0;
        Hyprutils::Math::Vector2D offset;
    };

    bool CXdgShell::init(CDisplay* display) {
        m_display = display;

        m_global = wl_global_create(display->waylandDisplay(), &xdg_wm_base_interface, 6, this, &CXdgShell::bind);
        if (!m_global) {
            std::fprintf(stderr, "[colibry] failed to create xdg_wm_base global\n");
            return false;
        }

        return true;
    }

    void CXdgShell::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
        std::fprintf(stderr, "[colibry] client bound xdg_wm_base\n");

        const auto resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct xdg_wm_base_interface IMPLEMENTATION = {
            .destroy           = destroy,
            .create_positioner = createPositioner,
            .get_xdg_surface   = getXdgSurface,
            .pong              = pong,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, data, nullptr);
    }

    void CXdgShell::destroy(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CXdgShell::pong(wl_client* /*client*/, wl_resource* /*resource*/, uint32_t /*serial*/) {
        // Ping/pong keepalive; nothing to verify.
    }

    void CXdgShell::createPositioner(wl_client* client, wl_resource* /*resource*/, uint32_t id) {
        const auto resource = wl_resource_create(client, &xdg_positioner_interface, 6, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        auto* positioner = new SPositioner();

        static const struct xdg_positioner_interface IMPLEMENTATION = {
            .destroy =
                [](wl_client*, wl_resource* res) {
                    wl_resource_destroy(res);
                },
            .set_size =
                [](wl_client*, wl_resource* res, int32_t w, int32_t h) {
                    static_cast<SPositioner*>(wl_resource_get_user_data(res))->size = {w, h};
                },
            .set_anchor_rect =
                [](wl_client*, wl_resource* res, int32_t x, int32_t y, int32_t w, int32_t h) {
                    static_cast<SPositioner*>(wl_resource_get_user_data(res))->anchorRect = {{x, y}, {w, h}};
                },
            .set_anchor =
                [](wl_client*, wl_resource* res, uint32_t anchor) {
                    static_cast<SPositioner*>(wl_resource_get_user_data(res))->anchor = anchor;
                },
            .set_gravity =
                [](wl_client*, wl_resource* res, uint32_t gravity) {
                    static_cast<SPositioner*>(wl_resource_get_user_data(res))->gravity = gravity;
                },
            .set_constraint_adjustment =
                [](wl_client*, wl_resource* res, uint32_t adjustment) {
                    static_cast<SPositioner*>(wl_resource_get_user_data(res))->constraintAdjustment = adjustment;
                },
            .set_offset =
                [](wl_client*, wl_resource* res, int32_t x, int32_t y) {
                    static_cast<SPositioner*>(wl_resource_get_user_data(res))->offset = {x, y};
                },
            .set_reactive =
                [](wl_client*, wl_resource*) {
                    // Reactive popups are re-positioned on parent changes;
                    // the popup stage consumes this flag.
                },
            .set_parent_size =
                [](wl_client*, wl_resource*, int32_t, int32_t) {
                    // Parent dimensions feed constraint solving during popup
                    // placement in the popup stage.
                },
            .set_parent_configure =
                [](wl_client*, wl_resource*, uint32_t) {
                    // Serial of the parent configure; consumed together with
                    // the parent size during popup placement.
                },
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, positioner, [](wl_resource* res) { delete static_cast<SPositioner*>(wl_resource_get_user_data(res)); });
    }

    void CXdgShell::getXdgSurface(wl_client* client, wl_resource* /*resource*/, uint32_t id, wl_resource* surfaceResource) {
        const auto surface = static_cast<CSurface*>(wl_resource_get_user_data(surfaceResource));

        const auto resource = wl_resource_create(client, &xdg_surface_interface, 6, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        auto* data     = new SXdgSurface();
        data->surface  = surface;
        data->resource = resource;

        static const struct xdg_surface_interface IMPLEMENTATION = {
            .destroy             = surfaceDestroy,
            .get_toplevel        = getToplevel,
            .get_popup           = getPopup,
            .set_window_geometry = setWindowGeometry,
            .ack_configure       = ackConfigure,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, data, [](wl_resource* res) {
            auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(res));
            if (data->surface)
                data->surface->onCommit = nullptr;
            delete data;
            wl_resource_set_user_data(res, nullptr);
        });
    }

    void CXdgShell::sendInitialConfigure(wl_resource* xdgSurfaceResource) {
        if (!xdgSurfaceResource)
            return;

        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(xdgSurfaceResource));
        if (!data || data->initialConfigureSent)
            return;

        data->initialConfigureSent = true;
        data->configureSerial      = wl_display_next_serial(wl_client_get_display(wl_resource_get_client(xdgSurfaceResource)));

        if (data->toplevelResource) {
            struct wl_array states;
            wl_array_init(&states);
            xdg_toplevel_send_configure(data->toplevelResource, 0, 0, &states);
            wl_array_release(&states);
        }
        xdg_surface_send_configure(xdgSurfaceResource, data->configureSerial);
    }

    void CXdgShell::surfaceDestroy(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CXdgShell::getToplevel(wl_client* client, wl_resource* resource, uint32_t id) {
        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(resource));

        const auto toplevel = wl_resource_create(client, &xdg_toplevel_interface, 6, id);
        if (!toplevel) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct xdg_toplevel_interface IMPLEMENTATION = {
            .destroy          = toplevelDestroy,
            .set_parent       = setParent,
            .set_title        = setTitle,
            .set_app_id       = setAppId,
            .show_window_menu = showWindowMenu,
            .move             = move,
            .resize           = resize,
            .set_max_size     = setMaxSize,
            .set_min_size     = setMinSize,
            .set_maximized    = setMaximized,
            .unset_maximized  = unsetMaximized,
            .set_fullscreen   = setFullscreen,
            .unset_fullscreen = unsetFullscreen,
            .set_minimized    = setMinimized,
        };

        auto* window = new CWindow(data->surface, toplevel, resource);
        data->window           = Hyprutils::Memory::CSharedPointer<CWindow>(window);
        data->toplevelResource = toplevel;

        wl_resource_set_implementation(toplevel, &IMPLEMENTATION, data, nullptr);
    }

    void CXdgShell::getPopup(wl_client* client, wl_resource* resource, uint32_t id, wl_resource* /*parent*/, wl_resource* positioner) {
        const auto popup = wl_resource_create(client, &xdg_popup_interface, 6, id);
        if (!popup) {
            wl_client_post_no_memory(client);
            return;
        }

        auto* positionerData = static_cast<SPositioner*>(wl_resource_get_user_data(positioner));
        (void)positionerData;
        (void)resource;

        static const struct xdg_popup_interface IMPLEMENTATION = {
            .destroy    = [](wl_client*, wl_resource* res) { wl_resource_destroy(res); },
            .grab       = [](wl_client*, wl_resource*, wl_resource*, uint32_t) {},
            .reposition = [](wl_client*, wl_resource*, wl_resource*, uint32_t) {},
        };

        wl_resource_set_implementation(popup, &IMPLEMENTATION, nullptr, nullptr);
    }

    void CXdgShell::setWindowGeometry(wl_client* /*client*/, wl_resource* /*resource*/, int32_t x, int32_t y, int32_t w, int32_t h) {
        (void)x;
        (void)y;
        (void)w;
        (void)h;
    }

    void CXdgShell::ackConfigure(wl_client* /*client*/, wl_resource* resource, uint32_t serial) {
        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(resource));
        if (data && serial == data->configureSerial)
            data->initialConfigureSent = true;
    }

    void CXdgShell::toplevelDestroy(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CXdgShell::setParent(wl_client* /*client*/, wl_resource* /*resource*/, wl_resource* /*parent*/) {
        // Transient parenting is applied when the switcher and stacking
        // order land in the desktop stage.
    }

    void CXdgShell::setTitle(wl_client* /*client*/, wl_resource* resource, const char* title) {
        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(resource));
        if (data && data->window)
            data->window->setTitle(title ? title : "");
    }

    void CXdgShell::setAppId(wl_client* /*client*/, wl_resource* /*resource*/, const char* /*appId*/) {
        // The application id groups windows; consumed by window rules.
    }

    void CXdgShell::showWindowMenu(wl_client*, wl_resource*, wl_resource*, uint32_t, int32_t, int32_t) {
        // The custom title bar replaces the window menu.
    }

    void CXdgShell::move(wl_client*, wl_resource*, wl_resource*, uint32_t) {
        // Interactive move is handled by the compositor drag manager.
    }

    void CXdgShell::resize(wl_client*, wl_resource*, wl_resource*, uint32_t, uint32_t) {
        // Interactive resize is handled by the compositor drag manager.
    }

    void CXdgShell::setMaxSize(wl_client*, wl_resource*, int32_t, int32_t) {
        // Size constraints are enforced when tiled states arrive.
    }

    void CXdgShell::setMinSize(wl_client*, wl_resource*, int32_t, int32_t) {
        // Size constraints are enforced when tiled states arrive.
    }

    void CXdgShell::setMaximized(wl_client*, wl_resource* resource) {
        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(resource));
        if (data && data->window)
            data->window->toggleMaximized();
    }

    void CXdgShell::unsetMaximized(wl_client*, wl_resource* resource) {
        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(resource));
        if (data && data->window && data->window->maximized())
            data->window->toggleMaximized();
    }

    void CXdgShell::setFullscreen(wl_client*, wl_resource* resource, wl_resource*) {
        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(resource));
        if (!data || !data->window)
            return;

        auto& compositor = CCompositor::get();

        if (!data->window->mapped()) {
            // A window that asks for fullscreen before its first commit goes
            // to a fresh empty workspace and opens tiled across it, not in
            // protocol fullscreen.
            compositor.moveWindowToWorkspace(data->window.get(), compositor.firstEmptyWorkspace());
            data->window->openTiledFullscreen();
            return;
        }

        data->window->setFullscreen(true);
    }

    void CXdgShell::unsetFullscreen(wl_client*, wl_resource* resource) {
        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(resource));
        if (data && data->window)
            data->window->setFullscreen(false);
    }

    void CXdgShell::setMinimized(wl_client*, wl_resource* resource) {
        auto* data = static_cast<SXdgSurface*>(wl_resource_get_user_data(resource));
        if (data && data->window)
            data->window->setMinimized(true);
    }

}
