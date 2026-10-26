// Implementation of wl_surface and subsurface state.

#include "Surface.hpp"

#include "Display.hpp"

#include <algorithm>
#include <wayland-server-protocol.h>

// Magic identifying dmabuf wl_buffer user data created through linux-dmabuf.
#include "DMABuf.hpp"

namespace ColibryShell {

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
            .offset               = offset,
        };

        auto* surface = new CSurface();
        surface->m_resource = resource;
        surface->m_display  = static_cast<CDisplay*>(wl_resource_get_user_data(compositor));

        surface->m_self = Hyprutils::Memory::CSharedPointer<CSurface>(surface);

        wl_resource_set_implementation(resource, &IMPLEMENTATION, surface,
                                       [](wl_resource* res) { static_cast<CSurface*>(wl_resource_get_user_data(res))->destroyResource(); });

        surface->m_display->registerSurface(surface->m_self);
    }

    void CSurface::destroyResource() {
        if (m_display)
            m_display->unregisterSurface(this);

        if (m_parent) {
            m_parent->removeSubsurface(this);
            m_parent = nullptr;
        }
        for (const auto sub : m_subsurfaces)
            wl_resource_destroy(sub->resource());
        m_subsurfaces.clear();

        releaseBuffer();

        for (const auto cb : m_frameCallbacks)
            wl_resource_destroy(cb);
        m_frameCallbacks.clear();
        for (const auto cb : m_pendingFrameCallbacks)
            wl_resource_destroy(cb);
        m_pendingFrameCallbacks.clear();

        wl_resource_set_user_data(m_resource, nullptr);
        wl_resource_destroy(m_resource);

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

    const SSurfaceDMA& CSurface::currentDMA() const {
        return m_currentDMA;
    }

    CSurface* CSurface::parent() const {
        return m_parent;
    }

    void CSurface::attachSubsurface(CSurface* parent) {
        m_parent = parent;
    }

    void CSurface::detachSubsurface() {
        m_parent = nullptr;
    }

    const std::vector<CSurface*>& CSurface::subsurfaces() const {
        return m_subsurfaces;
    }

    void CSurface::addSubsurface(CSurface* sub) {
        m_subsurfaces.push_back(sub);
    }

    void CSurface::removeSubsurface(CSurface* sub) {
        std::erase(m_subsurfaces, sub);
    }

    void CSurface::raiseSubsurface(CSurface* sub, CSurface* sibling, bool above) {
        const auto subIt = std::find(m_subsurfaces.begin(), m_subsurfaces.end(), sub);
        const auto sibIt = std::find(m_subsurfaces.begin(), m_subsurfaces.end(), sibling);
        if (subIt == m_subsurfaces.end() || sibIt == m_subsurfaces.end())
            return;

        m_subsurfaces.erase(subIt);
        const auto insertAt = std::find(m_subsurfaces.begin(), m_subsurfaces.end(), sibling);
        m_subsurfaces.insert(above ? insertAt + 1 : insertAt, sub);
    }

    Hyprutils::Math::Vector2D CSurface::subsurfaceOffset() const {
        return m_subsurfaceOffset;
    }

    void CSurface::setSubsurfaceOffset(int32_t x, int32_t y) {
        m_subsurfaceOffset = {(double)x, (double)y};
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

        surface->m_pendingDMA = {};
        surface->m_pendingBuffer.reset();

        if (!bufferResource) {
            surface->m_foreignBuffer = false;
            (void)dx;
            (void)dy;
            return;
        }

        // Shared memory buffers carry a CBuffer.
        if (wl_shm_buffer_get(bufferResource)) {
            auto* buffer = static_cast<CBuffer*>(wl_resource_get_user_data(bufferResource));
            surface->m_pendingBuffer = buffer ? buffer->self() : Hyprutils::Memory::CSharedPointer<CBuffer>(nullptr);
            surface->m_foreignBuffer = false;
            return;
        }

        // Dmabuf buffers created through linux-dmabuf carry attributes.
        auto* attrs = static_cast<SDMABufAttrs*>(wl_resource_get_user_data(bufferResource));
        if (attrs && attrs->valid) {
            surface->m_pendingDMA.valid    = true;
            surface->m_pendingDMA.fd       = attrs->fd;
            surface->m_pendingDMA.width    = attrs->width;
            surface->m_pendingDMA.height   = attrs->height;
            surface->m_pendingDMA.format   = attrs->format;
            surface->m_pendingDMA.modifier = attrs->modifier;
            surface->m_pendingDMA.stride   = attrs->stride;
            surface->m_pendingDMA.offset   = attrs->offset;
            surface->m_foreignBuffer       = false;
            return;
        }

        surface->m_foreignBuffer = true;
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

    void CSurface::setOpaqueRegion(wl_client*, wl_resource*, wl_resource*) {
        // Opaque regions only matter for optimization; ignored.
    }

    void CSurface::setInputRegion(wl_client*, wl_resource*, wl_resource*) {
        // Input regions are consumed by hit testing.
    }

    void CSurface::commit(wl_client*, wl_resource* resource) {
        auto* surface = static_cast<CSurface*>(wl_resource_get_user_data(resource));

        surface->releaseBuffer();
        surface->m_currentBuffer  = surface->m_pendingBuffer;
        surface->m_pendingBuffer.reset();
        surface->m_currentDMA     = surface->m_pendingDMA;
        surface->m_pendingDMA     = {};
        surface->m_mapped         = surface->m_currentBuffer != nullptr || surface->m_currentDMA.valid || surface->m_foreignBuffer;
        surface->m_damage         = surface->m_pendingDamage;
        surface->m_pendingDamage.clear();
        surface->m_frameCallbacks = std::move(surface->m_pendingFrameCallbacks);

        if (surface->m_currentBuffer)
            surface->m_currentBuffer->retain();

        if (surface->onCommit)
            surface->onCommit(surface);
    }

    void CSurface::setBufferTransform(wl_client*, wl_resource*, int32_t) {
        // Buffer transforms are not supported.
    }

    void CSurface::setBufferScale(wl_client*, wl_resource*, int32_t) {
        // HiDPI buffer scales are handled through output scaling.
    }

    void CSurface::damageBuffer(wl_client* client, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h) {
        damage(client, resource, x, y, w, h);
    }

    void CSurface::offset(wl_client*, wl_resource*, int32_t, int32_t) {
        // wl_surface v6 default offset; surfaces are positioned by the
        // compositor, so the request is accepted and ignored.
    }

}
