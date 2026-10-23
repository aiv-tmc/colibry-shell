// Implementation of wl_surface.

#include "Surface.hpp"

#include "Display.hpp"

#include <algorithm>
#include <wayland-server-protocol.h>

namespace Colibry {

    void CSurface::create(wl_client* client, wl_resource* compositor, uint32_t id) {
        const auto resource = wl_resource_create(client, &wl_surface_interface, 6, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_surface_interface IMPLEMENTATION = {
            .destroy              = destroy,
            .attach               = attach,
            .damage               = damage,
            .frame                = frame,
            .set_opaque_region    = setOpaqueRegion,
            .set_input_region     = setInputRegion,
            .commit               = commit,
            .set_buffer_transform = setBufferTransform,
            .set_buffer_scale     = setBufferScale,
            .damage_buffer        = damageBuffer,
        };

        auto* surface = new CSurface();
        surface->m_resource = resource;
        surface->m_display  = static_cast<CDisplay*>(wl_resource_get_user_data(compositor));

        // Adopts the object and keeps it alive through the self reference
        // until destroyResource drops it.
        surface->m_self = Hyprutils::Memory::CSharedPointer<CSurface>(surface);

        wl_resource_set_implementation(resource, &IMPLEMENTATION, surface,
                                       [](wl_resource* res) { static_cast<CSurface*>(wl_resource_get_user_data(res))->destroyResource(); });

        // The display owns a second reference for the surface list.
        surface->m_display->registerSurface(surface->m_self);
    }

    void CSurface::destroyResource() {
        if (m_display)
            m_display->unregisterSurface(this);

        releaseBuffer();

        for (const auto cb : m_frameCallbacks)
            wl_resource_destroy(cb);
        m_frameCallbacks.clear();
        for (const auto cb : m_pendingFrameCallbacks)
            wl_resource_destroy(cb);
        m_pendingFrameCallbacks.clear();

        wl_resource_set_user_data(m_resource, nullptr);
        wl_resource_destroy(m_resource);

        // Drops the self reference; the object is freed once the display
        // list released its reference above.
        m_self.reset();
    }

    wl_resource* CSurface::resource() const {
        return m_resource;
    }

    Hyprutils::Memory::CSharedPointer<CBuffer> CSurface::currentBuffer() const {
        return m_currentBuffer;
    }

    const Hyprutils::Math::CRegion& CSurface::damage() const {
        return m_damage;
    }

    bool CSurface::mapped() const {
        return m_mapped;
    }

    void CSurface::sendFrameDone(uint32_t timeMs) {
        for (const auto cb : m_frameCallbacks)
            wl_callback_send_done(cb, timeMs);
        for (const auto cb : m_frameCallbacks)
            wl_resource_destroy(cb);
        m_frameCallbacks.clear();
    }

    void CSurface::clearDamage() {
        m_damage.clear();
    }

    void CSurface::releaseBuffer() {
        if (m_currentBuffer) {
            m_currentBuffer->release();
            m_currentBuffer.reset();
        }
    }

    void CSurface::destroy(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CSurface::attach(wl_client* /*client*/, wl_resource* resource, wl_resource* bufferResource, int32_t dx, int32_t dy) {
        auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(resource));

        CBuffer* buffer = nullptr;
        if (bufferResource)
            buffer = static_cast<CBuffer*>(wl_resource_get_user_data(bufferResource));

        // Shares the buffer without owning the wayland resource.
        surface->m_pendingBuffer = buffer ? buffer->self() : Hyprutils::Memory::CSharedPointer<CBuffer>(nullptr);
        (void)dx;
        (void)dy;
    }

    void CSurface::damage(wl_client* /*client*/, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h) {
        auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(resource));
        surface->m_pendingDamage.add(Hyprutils::Math::CBox({x, y}, {w, h}));
    }

    void CSurface::frame(wl_client* client, wl_resource* resource, uint32_t callbackId) {
        const auto callback = wl_resource_create(client, &wl_callback_interface, 1, callbackId);
        if (!callback) {
            wl_client_post_no_memory(client);
            return;
        }

        auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(resource));
        surface->m_pendingFrameCallbacks.push_back(callback);
    }

    void CSurface::setOpaqueRegion(wl_client* /*client*/, wl_resource* /*resource*/, wl_resource* /*region*/) {
        // Opaque regions only matter for optimization; ignored at this stage.
    }

    void CSurface::setInputRegion(wl_client* /*client*/, wl_resource* /*resource*/, wl_resource* /*region*/) {
        // Input regions are consumed by the hit testing in the input stage.
    }

    void CSurface::commit(wl_client* /*client*/, wl_resource* resource) {
        auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(resource));

        // Swaps the pending state into the current state.
        surface->releaseBuffer();
        surface->m_currentBuffer  = surface->m_pendingBuffer;
        surface->m_pendingBuffer.reset();
        surface->m_mapped         = surface->m_currentBuffer != nullptr;
        surface->m_damage         = surface->m_pendingDamage;
        surface->m_pendingDamage.clear();
        surface->m_frameCallbacks = std::move(surface->m_pendingFrameCallbacks);

        if (surface->m_currentBuffer)
            surface->m_currentBuffer->retain();

        if (surface->onCommit)
            surface->onCommit(surface);
    }

    void CSurface::setBufferTransform(wl_client* /*client*/, wl_resource* /*resource*/, int32_t /*transform*/) {
        // Buffer transforms are not supported at this stage.
    }

    void CSurface::setBufferScale(wl_client* /*client*/, wl_resource* /*resource*/, int32_t /*scale*/) {
        // HiDPI buffer scales are handled through output scaling instead.
    }

    void CSurface::damageBuffer(wl_client* client, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h) {
        // The compositor renders buffers 1:1 in buffer-local coordinates, so
        // damage_buffer and damage carry the same meaning here.
        damage(client, resource, x, y, w, h);
    }

}
