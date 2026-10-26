// linux-dmabuf protocol global for the Colibry compositor.
//
// Only what the GPU renderer consumes is implemented: plane 0 of
// ARGB8888/XRGB8888 buffers with an explicit modifier. Buffer attributes
// are carried through wl_buffer user data and read by the surface commit
// path.

#pragma once

#include <wayland-server.h>

namespace ColibryShell {

    class CDisplay;

    // Client-visible attributes of a dmabuf-backed wl_buffer.
    struct SDMABufAttrs {
        bool     valid    = false;
        int32_t  fd       = -1;
        uint32_t width    = 0;
        uint32_t height   = 0;
        uint32_t format   = 0;
        uint64_t modifier = 0;
        uint32_t stride   = 0;
        uint32_t offset   = 0;
    };

    class CDMABufManager {
      public:
        static CDMABufManager& get();

        // Creates the zwp_linux_dmabuf_v1 global.
        bool init(CDisplay* display);

        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);

        // Protocol request handlers. Signatures match the generated v1
        // interface exactly (plane_idx is unsigned, sizes are signed).
        static void destroy(wl_client* client, wl_resource* resource);
        static void createParams(wl_client* client, wl_resource* resource, uint32_t id);
        static void paramsDestroy(wl_client* client, wl_resource* resource);
        static void paramsAdd(wl_client* client, wl_resource* resource, int32_t fd, uint32_t planeIdx, uint32_t offset, uint32_t stride,
                              uint32_t modifierHi, uint32_t modifierLo);
        static void paramsCreate(wl_client* client, wl_resource* resource, int32_t width, int32_t height, uint32_t format, uint32_t flags);
        static void paramsCreateImmed(wl_client* client, wl_resource* resource, uint32_t bufferId, int32_t width, int32_t height,
                                      uint32_t format, uint32_t flags);

      private:
        CDMABufManager()  = default;
        ~CDMABufManager() = default;

        CDMABufManager(const CDMABufManager&)            = delete;
        CDMABufManager& operator=(const CDMABufManager&) = delete;

        CDisplay*   m_display = nullptr;
        wl_global*  m_global  = nullptr;
    };

}
