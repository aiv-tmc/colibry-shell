// linux-dmabuf protocol global for the ColibryShell compositor.
//
// Plane 0 of the 32-bit RGB formats only. The manager can be disabled at
// runtime: once disabled, buffer creation requests fail with a protocol
// error and clients fall back to wl_shm, which this renderer imports
// through the proven CPU upload path.

#pragma once

#include <wayland-server.h>

namespace ColibryShell {

    class CDisplay;

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

        bool init(CDisplay* display);

        // Runtime kill switch: after a failed GPU import the renderer
        // disables the manager so new buffers cannot be created and
        // clients migrate to wl_shm.
        void disable();
        bool disabled() const { return m_disabled; }

        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);

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

        CDisplay* m_display  = nullptr;
        wl_global* m_global  = nullptr;
        bool       m_disabled = false;
    };

}
