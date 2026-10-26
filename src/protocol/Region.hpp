// wl_region implementation for the Colibry compositor.
//
// CRegionResource stores a Hyprutils region built from the client's
// add/subtract requests. Regions are attached to surfaces as opaque/input
// regions and used by hit testing.

#pragma once

#include <hyprutils/math/Region.hpp>

#include <wayland-server.h>

namespace ColibryShell {

    class CRegionResource {
      public:
        // wl_compositor.create_region handler: creates the region resource.
        static void create(wl_client* client, wl_resource* compositor, uint32_t id);

        // The accumulated region in surface-local coordinates.
        const Hyprutils::Math::CRegion& region() const;

      private:
        CRegionResource() = default;

        static void add(wl_client* client, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h);
        static void subtract(wl_client* client, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h);
        static void destroy(wl_client* client, wl_resource* resource);

        Hyprutils::Math::CRegion m_region;
    };

}
