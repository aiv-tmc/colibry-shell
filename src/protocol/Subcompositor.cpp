// Implementation of wl_subcompositor.

#include "Subcompositor.hpp"

#include "Display.hpp"
#include "Surface.hpp"

#include <algorithm>
#include <cstdio>

#include <wayland-server-protocol.h>

namespace Colibry {

    CSubcompositor& CSubcompositor::get() {
        static CSubcompositor s_instance;
        return s_instance;
    }

    bool CSubcompositor::init(CDisplay* display) {
        m_display = display;

        m_global = wl_global_create(display->waylandDisplay(), &wl_subcompositor_interface, 1, this, &CSubcompositor::bind);
        if (!m_global) {
            std::fprintf(stderr, "[colibry] failed to create wl_subcompositor global\n");
            return false;
        }

        return true;
    }

    void CSubcompositor::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
        std::fprintf(stderr, "[colibry] client bound wl_subcompositor\n");

        const auto resource = wl_resource_create(client, &wl_subcompositor_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_subcompositor_interface IMPLEMENTATION = {
            .destroy        = destroy,
            .get_subsurface = getSubsurface,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, data, nullptr);
    }

    void CSubcompositor::destroy(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CSubcompositor::getSubsurface(wl_client* client, wl_resource* /*resource*/, uint32_t id, wl_resource* surfaceResource,
                                       wl_resource* parentResource) {
        auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(surfaceResource));
        auto* parent  = static_cast<CSurface*>(wl_resource_get_user_data(parentResource));

        if (!surface || !parent || surface == parent || surface->parent()) {
            wl_resource_post_error(surfaceResource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "invalid subsurface");
            return;
        }

        const auto resource = wl_resource_create(client, &wl_subsurface_interface, 1, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_subsurface_interface IMPLEMENTATION = {
            .destroy      = subDestroy,
            .set_position = setPosition,
            .place_above  = placeAbove,
            .place_below  = placeBelow,
        };

        surface->attachSubsurface(parent);
        parent->addSubsurface(surface);

        wl_resource_set_implementation(resource, &IMPLEMENTATION, surface, [](wl_resource* res) {
            auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(res));
            if (surface && surface->parent()) {
                surface->parent()->removeSubsurface(surface);
                surface->detachSubsurface();
            }
        });
    }

    void CSubcompositor::setPosition(wl_client* /*client*/, wl_resource* resource, int32_t x, int32_t y) {
        if (auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(resource)))
            surface->setSubsurfaceOffset(x, y);
    }

    void CSubcompositor::placeAbove(wl_client*, wl_resource* resource, wl_resource* siblingResource) {
        auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(resource));
        auto* sibling = static_cast<CSurface*>(wl_resource_get_user_data(siblingResource));

        if (surface && sibling && surface->parent() == sibling->parent() && surface->parent())
            surface->parent()->raiseSubsurface(surface, sibling, true);
    }

    void CSubcompositor::placeBelow(wl_client*, wl_resource* resource, wl_resource* siblingResource) {
        auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(resource));
        auto* sibling = static_cast<CSurface*>(wl_resource_get_user_data(siblingResource));

        if (surface && sibling && surface->parent() == sibling->parent() && surface->parent())
            surface->parent()->raiseSubsurface(surface, sibling, false);
    }

    void CSubcompositor::subDestroy(wl_client*, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

}
