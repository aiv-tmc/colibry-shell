// Implementation of shared memory buffers and pools.

#include "Buffer.hpp"

#include <wayland-server-protocol.h>

#include <sys/mman.h>
#include <unistd.h>

namespace ColibryShell {

    // wl_shm_pool object data: the mapping owned by the pool and the wl_shm
    // resource used to report protocol errors.
    struct SShmPool {
        uint8_t*     data        = nullptr;
        int32_t      size        = 0;
        wl_resource* shmResource = nullptr;
    };

    CBuffer::CBuffer(wl_resource* resource, uint8_t* data, Hyprutils::Math::Vector2D size, int32_t stride, uint32_t format) :
        m_resource(resource), m_data(data), m_size(size), m_stride(stride), m_format(format) {
        // Adopts the object into a self reference so other parts of the
        // compositor can share it without owning the wayland resource.
        m_self = Hyprutils::Memory::CSharedPointer<CBuffer>(this);
    }

    Hyprutils::Memory::CSharedPointer<CBuffer> CBuffer::self() {
        return m_self;
    }

    void CBuffer::dropSelf() {
        m_self.reset();
    }

    uint8_t* CBuffer::data() const {
        return m_data;
    }

    int32_t CBuffer::stride() const {
        return m_stride;
    }

    Hyprutils::Math::Vector2D CBuffer::size() const {
        return m_size;
    }

    uint32_t CBuffer::format() const {
        return m_format;
    }

    wl_resource* CBuffer::resource() const {
        return m_resource;
    }

    void CBuffer::retain() {
        ++m_retained;
    }

    void CBuffer::release() {
        if (m_retained > 0)
            --m_retained;

        // Tells the client the pixels may be reused; safe because shm buffers
        // are fully copied by the compositor before this point. Skipped when
        // the client already destroyed the wl_buffer resource.
        if (m_retained == 0 && m_resource)
            wl_buffer_send_release(m_resource);
    }

    void CShm::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
        const auto resource = wl_resource_create(client, &wl_shm_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_shm_interface IMPLEMENTATION = {
            .create_pool = createPool,
            .release     = release,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, data, nullptr);

        // The only pixel formats the compositor renders at this stage.
        wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
        wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
    }

    void CShm::createPool(wl_client* client, wl_resource* resource, uint32_t id, int32_t fd, int32_t size) {
        if (size <= 0) {
            wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "invalid pool size");
            close(fd);
            return;
        }

        // Maps the client-provided fd read-only; the pool owns the mapping.
        auto* mapping = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
        close(fd);

        if (mapping == MAP_FAILED) {
            wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "failed to mmap pool");
            return;
        }

        const auto poolResource = wl_resource_create(client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);
        if (!poolResource) {
            munmap(mapping, size);
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_shm_pool_interface POOL_IMPLEMENTATION = {
            .create_buffer = poolCreateBuffer,
            .destroy       = poolDestroy,
            .resize        = poolResize,
        };

        auto* pool = new SShmPool{.data = mapping, .size = size, .shmResource = resource};
        wl_resource_set_implementation(poolResource, &POOL_IMPLEMENTATION, pool, [](wl_resource* res) {
            auto* pool = static_cast<SShmPool*>(wl_resource_get_user_data(res));
            munmap(pool->data, pool->size);
            delete pool;
        });
    }

    void CShm::release(wl_client* /*client*/, wl_resource* /*resource*/) {
        // The pool resources are owned by clients; nothing to free here.
    }

    void CShm::poolCreateBuffer(wl_client* client, wl_resource* resource, uint32_t id, int32_t offset, int32_t width, int32_t height,
                                int32_t stride, uint32_t format) {
        auto* pool = static_cast<SShmPool*>(wl_resource_get_user_data(resource));

        if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888) {
            wl_resource_post_error(pool->shmResource, WL_SHM_ERROR_INVALID_FORMAT, "unsupported buffer format");
            return;
        }

        if (width <= 0 || height <= 0 || offset < 0 || stride < width * 4 || offset + stride * (int64_t)height > pool->size) {
            wl_resource_post_error(pool->shmResource, WL_SHM_ERROR_INVALID_FD, "buffer out of pool bounds");
            return;
        }

        const auto bufferResource = wl_resource_create(client, &wl_buffer_interface, 1, id);
        if (!bufferResource) {
            wl_client_post_no_memory(client);
            return;
        }

        auto* buffer = new CBuffer(bufferResource, pool->data + offset, {width, height}, stride, format);

        // The CBuffer lifetime follows the wl_buffer resource; surfaces keep
        // their own references through CBuffer::self().
        wl_resource_set_implementation(bufferResource, nullptr, buffer,
                                       [](wl_resource* res) { static_cast<CBuffer*>(wl_resource_get_user_data(res))->dropSelf(); });
    }

    void CShm::poolDestroy(wl_client* /*client*/, wl_resource* resource) {
        // Destroys the resource; the mapping is freed by the destructor.
        wl_resource_destroy(resource);
    }

    void CShm::poolResize(wl_client* /*client*/, wl_resource* resource, int32_t size) {
        auto* pool = static_cast<SShmPool*>(wl_resource_get_user_data(resource));

        if (size < pool->size) {
            wl_resource_post_error(pool->shmResource, WL_SHM_ERROR_INVALID_FD, "cannot shrink a pool");
            return;
        }

        // Grows the mapping in place; previously returned pointers stay
        // valid according to the wl_shm_pool.resize contract.
        auto* grown = mremap(pool->data, pool->size, size, MREMAP_MAYMOVE);
        if (grown == MAP_FAILED) {
            wl_resource_post_error(pool->shmResource, WL_SHM_ERROR_INVALID_FD, "failed to grow pool");
            return;
        }

        pool->data = static_cast<uint8_t*>(grown);
        pool->size = size;
    }

}
