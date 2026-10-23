// Implementation of wl_region.

#include "Region.hpp"

#include <wayland-server-protocol.h>

namespace Colibry {

    void CRegionResource::create(wl_client* client, wl_resource* /*compositor*/, uint32_t id) {
        const auto resource = wl_resource_create(client, &wl_region_interface, 1, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_region_interface IMPLEMENTATION = {
            .destroy  = destroy,
            .add      = add,
            .subtract = subtract,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, new CRegionResource,
                                       [](wl_resource* res) { delete static_cast<CRegionResource*>(wl_resource_get_user_data(res)); });
    }

    const Hyprutils::Math::CRegion& CRegionResource::region() const {
        return m_region;
    }

    void CRegionResource::add(wl_client* /*client*/, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h) {
        static_cast<CRegionResource*>(wl_resource_get_user_data(resource))->m_region.add(Hyprutils::Math::CBox({x, y}, {w, h}));
    }

    void CRegionResource::subtract(wl_client* /*client*/, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h) {
        static_cast<CRegionResource*>(wl_resource_get_user_data(resource))->m_region.subtract(Hyprutils::Math::CBox({x, y}, {w, h}));
    }

    void CRegionResource::destroy(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

}
