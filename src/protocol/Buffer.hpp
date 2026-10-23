// Shared memory buffer support for the Colibry compositor.
//
// CShm implements the wl_shm global: clients create wl_shm_pool objects
// backed by mmap'ed files and derive wl_buffer resources from them. CBuffer
// wraps one wl_buffer holding the pixel data pointer, stride and format.

#pragma once

#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <cstdint>
#include <wayland-server.h>

namespace Colibry {

    class CBuffer {
      public:
        CBuffer(wl_resource* resource, uint8_t* data, Hyprutils::Math::Vector2D size, int32_t stride, uint32_t format);

        // Owning shared pointer of this buffer; used to reference the buffer
        // from surfaces without taking ownership of the resource lifetime.
        Hyprutils::Memory::CSharedPointer<CBuffer> self();
        // Drops the self reference; called when the wl_buffer resource is
        // destroyed. The object survives while surfaces still hold it.
        void dropSelf();

        // Pixel access for the renderer. Valid while the buffer is retained.
        uint8_t* data() const;
        int32_t stride() const;
        Hyprutils::Math::Vector2D size() const;
        uint32_t format() const;
        wl_resource* resource() const;

        // Increments the retain counter while the compositor reads pixels.
        void retain();
        // Decrements the counter and sends wl_buffer.release to the client
        // once the compositor no longer needs the pixels.
        void release();

      private:
        wl_resource*              m_resource;
        uint8_t*                  m_data;
        Hyprutils::Math::Vector2D m_size;
        int32_t                   m_stride;
        uint32_t                  m_format;

        // Owning self reference; reset on resource destruction.
        Hyprutils::Memory::CSharedPointer<CBuffer> m_self;

        // Number of in-flight read references held by the compositor.
        uint32_t m_retained = 0;
    };

    class CShm {
      public:
        // wl_shm global bind callback creating wl_shm resources.
        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);

      private:
        // wl_shm request: creates a memory pool from a file descriptor.
        static void createPool(wl_client* client, wl_resource* resource, uint32_t id, int32_t fd, int32_t size);
        // wl_shm request (since v2): releases a pool that no longer creates
        // buffers; buffers stay valid.
        static void release(wl_client* client, wl_resource* resource);

        // wl_shm_pool request: builds a wl_buffer covering a pool region.
        static void poolCreateBuffer(wl_client* client, wl_resource* resource, uint32_t id, int32_t offset, int32_t width,
                                     int32_t height, int32_t stride, uint32_t format);
        // wl_shm_pool request: destroys the pool once no buffers reference it.
        static void poolDestroy(wl_client* client, wl_resource* resource);
        // wl_shm_pool request: grows the visible tail of the pool mapping.
        static void poolResize(wl_client* client, wl_resource* resource, int32_t size);
    };

}
