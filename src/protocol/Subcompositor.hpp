// wl_subcompositor for the Colibry compositor.
//
// CSubcompositor implements wl_subcompositor so clients (GTK and Qt
// toolkits in particular) can create subsurfaces: child surfaces positioned
// relative to a parent, used for menus, tooltips and popups. Subsurface
// order within the parent is controlled by place_above/place_below.

#pragma once

#include <wayland-server.h>

namespace Colibry {

    class CDisplay;

    class CSubcompositor {
      public:
        static CSubcompositor& get();

        // Creates the wl_subcompositor global.
        bool init(CDisplay* display);

      private:
        CSubcompositor()  = default;
        ~CSubcompositor() = default;

        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);
        static void getSubsurface(wl_client* client, wl_resource* resource, uint32_t id, wl_resource* surface, wl_resource* parent);
        static void destroy(wl_client* client, wl_resource* resource);

        // wl_subsurface requests.
        static void setPosition(wl_client* client, wl_resource* resource, int32_t x, int32_t y);
        static void placeAbove(wl_client* client, wl_resource* resource, wl_resource* sibling);
        static void placeBelow(wl_client* client, wl_resource* resource, wl_resource* sibling);
        static void subDestroy(wl_client* client, wl_resource* resource);

        CDisplay*  m_display = nullptr;
        wl_global* m_global  = nullptr;
    };

}
