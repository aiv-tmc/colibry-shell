// Implementation of the foreign toplevel management protocol.

#include "ForeignToplevel.hpp"

#include "../core/Compositor.hpp"
#include "../desktop/Window.hpp"
#include "Display.hpp"
#include "OutputInfo.hpp"

#include <cstdio>

#include <wlr-foreign-toplevel-management-unstable-v1-server-protocol.h>

namespace Colibry {

    CForeignToplevelManager& CForeignToplevelManager::get() {
        static CForeignToplevelManager s_instance;
        return s_instance;
    }

    bool CForeignToplevelManager::init(CDisplay* display) {
        m_display = display;

        m_global = wl_global_create(display->waylandDisplay(), &zwlr_foreign_toplevel_manager_v1_interface, 3, this,
                                    &CForeignToplevelManager::bind);
        if (!m_global) {
            std::fprintf(stderr, "[colibry] failed to create the foreign toplevel manager global\n");
            return false;
        }

        return true;
    }

    void CForeignToplevelManager::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
        auto& self = get();

        const auto resource = wl_resource_create(client, &zwlr_foreign_toplevel_manager_v1_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct zwlr_foreign_toplevel_manager_v1_interface MANAGER_IMPLEMENTATION = {
            .stop = stop,
        };

        auto* clientEntry = new SClient{.resource = resource};
        wl_resource_set_implementation(resource, &MANAGER_IMPLEMENTATION, clientEntry, [](wl_resource* res) {
            auto& self  = get();
            auto* entry = static_cast<SClient*>(wl_resource_get_user_data(res));

            // Client disconnected: destroy its handles.
            for (auto& [window, handle] : entry->handles)
                wl_resource_destroy(handle.resource);

            delete entry;
            std::erase(self.m_clients, entry);
        });

        self.m_clients.push_back(clientEntry);

        // Existing windows are announced to the new client.
        for (const auto& window : CCompositor::get().m_windows) {
            if (window->mapped())
                self.createHandle(clientEntry, window.get());
        }
    }

    void CForeignToplevelManager::stop(wl_client*, wl_resource* resource) {
        auto* client = static_cast<SClient*>(wl_resource_get_user_data(resource));
        client->stopped = true;
    }

    void CForeignToplevelManager::createHandle(SClient* client, CWindow* window) {
        const auto handle = wl_resource_create(wl_resource_get_client(client->resource), &zwlr_foreign_toplevel_handle_v1_interface,
                                               wl_resource_get_version(client->resource), 0);
        if (!handle)
            return;

        static const struct zwlr_foreign_toplevel_handle_v1_interface HANDLE_IMPLEMENTATION = {
            .set_maximized    = handleSetMaximized,
            .unset_maximized  = handleUnsetMaximized,
            .set_minimized    = handleSetMinimized,
            .unset_minimized  = handleUnsetMinimized,
            .activate         = handleActivate,
            .close            = handleClose,
            .set_rectangle    = handleSetRectangle,
            .destroy          = handleDestroy,
            .set_fullscreen   = handleSetFullscreen,
            .unset_fullscreen = handleUnsetFullscreen,
        };

        client->handles[window] = SHandle{.resource = handle, .entered = false};
        SHandle* handleData     = &client->handles[window];

        wl_resource_set_implementation(handle, &HANDLE_IMPLEMENTATION, handleData, [](wl_resource* res) {
            auto& self = get();
            SHandle* data = static_cast<SHandle*>(wl_resource_get_user_data(res));

            for (auto* client : self.m_clients) {
                for (auto it = client->handles.begin(); it != client->handles.end(); ++it) {
                    if (&it->second == data) {
                        client->handles.erase(it);
                        return;
                    }
                }
            }
        });

        // The toplevel event transfers the new handle to the client.
        zwlr_foreign_toplevel_manager_v1_send_toplevel(client->resource, handle);
        zwlr_foreign_toplevel_handle_v1_send_title(handle, window->title().c_str());
        zwlr_foreign_toplevel_handle_v1_send_app_id(handle, window->appId().c_str());
        updateHandle(client, window);
    }

    void CForeignToplevelManager::updateHandle(SClient* client, CWindow* window) {
        const auto it = client->handles.find(window);
        if (it == client->handles.end())
            return;

        SHandle& handle = it->second;

        // State bits use the enum values of the protocol.
        struct wl_array states;
        wl_array_init(&states);

        const auto addState = [&states](uint32_t bit) {
            const auto value = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
            *value           = bit;
        };

        if (window->maximized())
            addState(ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED);
        if (window->minimized())
            addState(ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED);
        if (CCompositor::get().m_focusedWindow == window)
            addState(ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED);
        if (window->isFullscreen())
            addState(ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN);

        zwlr_foreign_toplevel_handle_v1_send_state(handle.resource, &states);
        wl_array_release(&states);

        // Output visibility mirrors workspace visibility on the first
        // output.
        const bool visible = window->visible();
        if (visible && !handle.entered) {
            if (const auto& outputs = CCompositor::get().m_outputs; !outputs.empty()) {
                if (wl_resource* output = COutputInfo::get().resourceFor(wl_resource_get_client(handle.resource), outputs.front()->aq()->name)) {
                    zwlr_foreign_toplevel_handle_v1_send_output_enter(handle.resource, output);
                    handle.entered = true;
                }
            }
        } else if (!visible && handle.entered) {
            if (const auto& outputs = CCompositor::get().m_outputs; !outputs.empty()) {
                if (wl_resource* output = COutputInfo::get().resourceFor(wl_resource_get_client(handle.resource), outputs.front()->aq()->name))
                    zwlr_foreign_toplevel_handle_v1_send_output_leave(handle.resource, output);
            }
            handle.entered = false;
        }

        zwlr_foreign_toplevel_handle_v1_send_done(handle.resource);
    }

    void CForeignToplevelManager::onWindowMapped(CWindow* window) {
        for (auto* client : m_clients) {
            if (!client->stopped)
                createHandle(client, window);
        }
    }

    void CForeignToplevelManager::onWindowUnmapped(CWindow* window) {
        for (auto* client : m_clients) {
            const auto it = client->handles.find(window);
            if (it == client->handles.end())
                continue;

            zwlr_foreign_toplevel_handle_v1_send_closed(it->second.resource);
            wl_resource_destroy(it->second.resource);
        }
    }

    void CForeignToplevelManager::onWindowTitle(CWindow* window) {
        for (auto* client : m_clients) {
            const auto it = client->handles.find(window);
            if (it == client->handles.end())
                continue;

            zwlr_foreign_toplevel_handle_v1_send_title(it->second.resource, window->title().c_str());
            zwlr_foreign_toplevel_handle_v1_send_done(it->second.resource);
        }
    }

    void CForeignToplevelManager::onWindowAppId(CWindow* window) {
        for (auto* client : m_clients) {
            const auto it = client->handles.find(window);
            if (it == client->handles.end())
                continue;

            zwlr_foreign_toplevel_handle_v1_send_app_id(it->second.resource, window->appId().c_str());
            zwlr_foreign_toplevel_handle_v1_send_done(it->second.resource);
        }
    }

    void CForeignToplevelManager::onWindowState(CWindow* window) {
        for (auto* client : m_clients) {
            if (!client->stopped && client->handles.contains(window))
                updateHandle(client, window);
        }
    }

    CWindow* CForeignToplevelManager::windowFor(wl_resource* handle) {
        SHandle* data = static_cast<SHandle*>(wl_resource_get_user_data(handle));

        for (auto* client : get().m_clients) {
            for (auto& [window, entry] : client->handles) {
                if (&entry == data)
                    return window;
            }
        }

        return nullptr;
    }

    void CForeignToplevelManager::handleSetMaximized(wl_client*, wl_resource* resource) {
        if (CWindow* window = windowFor(resource); window && !window->maximized())
            window->toggleMaximized();
    }

    void CForeignToplevelManager::handleUnsetMaximized(wl_client*, wl_resource* resource) {
        if (CWindow* window = windowFor(resource); window && window->maximized())
            window->toggleMaximized();
    }

    void CForeignToplevelManager::handleSetMinimized(wl_client*, wl_resource* resource) {
        if (CWindow* window = windowFor(resource); window)
            window->setMinimized(true);
    }

    void CForeignToplevelManager::handleUnsetMinimized(wl_client*, wl_resource* resource) {
        if (CWindow* window = windowFor(resource); window)
            window->setMinimized(false);
    }

    void CForeignToplevelManager::handleActivate(wl_client*, wl_resource* resource, wl_resource*) {
        if (CWindow* window = windowFor(resource); window)
            CCompositor::get().focusWindow(window);
    }

    void CForeignToplevelManager::handleClose(wl_client*, wl_resource* resource) {
        if (CWindow* window = windowFor(resource); window)
            window->close();
    }

    void CForeignToplevelManager::handleSetRectangle(wl_client*, wl_resource*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {
        // Representation hint of the panel; the compositor derives window
        // geometry from the window itself.
    }

    void CForeignToplevelManager::handleSetFullscreen(wl_client*, wl_resource* resource, wl_resource*) {
        if (CWindow* window = windowFor(resource); window)
            window->setFullscreen(true);
    }

    void CForeignToplevelManager::handleUnsetFullscreen(wl_client*, wl_resource* resource) {
        if (CWindow* window = windowFor(resource); window)
            window->setFullscreen(false);
    }

    void CForeignToplevelManager::handleDestroy(wl_client*, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

}
