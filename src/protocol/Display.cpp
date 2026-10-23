// Implementation of the Wayland display core.

#include "Display.hpp"

#include "Buffer.hpp"
#include "ForeignToplevel.hpp"
#include "OutputInfo.hpp"
#include "Region.hpp"
#include "Surface.hpp"

#include <wayland-server-protocol.h>

#include <cstdio>

namespace Colibry {

    bool CDisplay::init() {
        m_display = wl_display_create();
        if (!m_display) {
            std::fprintf(stderr, "[colibry] failed to create wl_display\n");
            return false;
        }

        m_compositorGlobal = wl_global_create(m_display, &wl_compositor_interface, 6, this, &CDisplay::bindCompositor);
        if (!m_compositorGlobal) {
            std::fprintf(stderr, "[colibry] failed to create wl_compositor global\n");
            return false;
        }

        m_shmGlobal = wl_global_create(m_display, &wl_shm_interface, 2, this, &CShm::bind);
        if (!m_shmGlobal) {
            std::fprintf(stderr, "[colibry] failed to create wl_shm global\n");
            return false;
        }

        // Output description globals (one per backend output, announced
        // later) and the foreign toplevel manager for panels.
        COutputInfo::get().init(this);
        if (!CForeignToplevelManager::get().init(this))
            std::fprintf(stderr, "[colibry] foreign toplevel management unavailable\n");

        const auto socketName = wl_display_add_socket_auto(m_display);
        if (!socketName) {
            std::fprintf(stderr, "[colibry] failed to create a wayland socket\n");
            return false;
        }

        m_socketName = socketName;
        std::fprintf(stderr, "[colibry] wayland socket: WAYLAND_DISPLAY=%s\n", m_socketName.c_str());
        return true;
    }

    void CDisplay::destroy() {
        if (m_display) {
            wl_display_destroy_clients(m_display);
            wl_display_destroy(m_display);
            m_display = nullptr;
        }
    }

    int CDisplay::fd() const {
        return wl_display_get_fd(m_display);
    }

    const std::string& CDisplay::socketName() const {
        return m_socketName;
    }

    void CDisplay::dispatch() {
        // Handles new clients, requests and timer/idle sources.
        wl_event_loop_dispatch(wl_display_get_event_loop(m_display), 0);
        // Flushes replies and events to every connected client.
        wl_display_flush_clients(m_display);
    }

    void CDisplay::sendFrameDone(uint32_t timeMs) {
        for (const auto& surface : m_surfaces)
            surface->sendFrameDone(timeMs);
    }

    void CDisplay::registerSurface(Hyprutils::Memory::CSharedPointer<CSurface> surface) {
        m_surfaces.push_back(std::move(surface));
    }

    void CDisplay::unregisterSurface(CSurface* surface) {
        for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
            if (it->get() == surface) {
                m_surfaces.erase(it);
                return;
            }
        }
    }

    const std::vector<Hyprutils::Memory::CSharedPointer<CSurface>>& CDisplay::surfaces() const {
        return m_surfaces;
    }

    wl_display* CDisplay::waylandDisplay() const {
        return m_display;
    }

    void CDisplay::bindCompositor(wl_client* client, void* data, uint32_t version, uint32_t id) {
        const auto self = static_cast<CDisplay*>(data);

        const auto resource = wl_resource_create(client, &wl_compositor_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_compositor_interface IMPLEMENTATION = {
            .create_surface = [](wl_client* c, wl_resource* res, uint32_t surfaceId) { CSurface::create(c, res, surfaceId); },
            .create_region  = [](wl_client* c, wl_resource* res, uint32_t regionId) { CRegionResource::create(c, res, regionId); },
        };

        // The resource user data carries the display so surface creation can
        // register new surfaces with it.
        wl_resource_set_implementation(resource, &IMPLEMENTATION, self, nullptr);
    }

}
