// wl_surface implementation for the Colibry compositor.
//
// CSurface tracks the double-buffered state of one client surface: pending
// and current buffers (shared memory CBuffer or dmabuf attributes received
// through linux-dmabuf), damage regions and frame callbacks. commit() swaps
// the pending state into the current state and notifies the desktop layer
// through a commit hook. Surfaces may be subsurfaces of a parent, carrying
// a position offset and a stacking order within the parent.

#pragma once

#include "Buffer.hpp"

#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <functional>
#include <vector>
#include <wayland-server.h>

namespace ColibryShell {

    class CDisplay;

    // Dmabuf plane attributes attached to a surface commit.
    struct SSurfaceDMA {
        bool     valid = false;
        int      fd    = -1; // borrowed, owned by the wl_buffer resource
        uint32_t width = 0, height = 0, format = 0;
        uint64_t modifier = 0;
        uint32_t stride = 0, offset = 0;
    };

    class CSurface {
      public:
        static void create(wl_client* client, wl_resource* compositor, uint32_t id);

        void destroyResource();

        wl_resource* resource() const;

        Hyprutils::Memory::CSharedPointer<CBuffer> currentBuffer() const;
        const Hyprutils::Math::CRegion&            damage() const;
        bool                                       mapped() const;

        // Dmabuf content of the current commit.
        const SSurfaceDMA& currentDMA() const;

        void sendFrameDone(uint32_t timeMs);
        void clearDamage();
        void releaseBuffer();

        CSurface* parent() const;
        void      attachSubsurface(CSurface* parent);
        void      detachSubsurface();
        const std::vector<CSurface*>& subsurfaces() const;
        void                          addSubsurface(CSurface* sub);
        void                          removeSubsurface(CSurface* sub);
        void                          raiseSubsurface(CSurface* sub, CSurface* sibling, bool above);
        Hyprutils::Math::Vector2D     subsurfaceOffset() const;
        void                          setSubsurfaceOffset(int32_t x, int32_t y);

        std::function<void(CSurface*)> onCommit;

      private:
        CSurface() = default;

        static void destroy(wl_client* client, wl_resource* resource);
        static void attach(wl_client* client, wl_resource* resource, wl_resource* bufferResource, int32_t dx, int32_t dy);
        static void damage(wl_client* client, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h);
        static void frame(wl_client* client, wl_resource* resource, uint32_t callbackId);
        static void setOpaqueRegion(wl_client* client, wl_resource* resource, wl_resource* region);
        static void setInputRegion(wl_client* client, wl_resource* resource, wl_resource* region);
        static void commit(wl_client* client, wl_resource* resource);
        static void setBufferTransform(wl_client* client, wl_resource* resource, int32_t transform);
        static void setBufferScale(wl_client* client, wl_resource* resource, int32_t scale);
        static void damageBuffer(wl_client* client, wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h);
        static void offset(wl_client* client, wl_resource* resource, int32_t x, int32_t y);

        wl_resource* m_resource = nullptr;
        CDisplay*    m_display  = nullptr;

        Hyprutils::Memory::CSharedPointer<CSurface> m_self;

        Hyprutils::Memory::CSharedPointer<CBuffer> m_pendingBuffer;
        Hyprutils::Memory::CSharedPointer<CBuffer> m_currentBuffer;
        Hyprutils::Math::CRegion                   m_pendingDamage;
        Hyprutils::Math::CRegion                   m_damage;

        SSurfaceDMA m_pendingDMA;
        SSurfaceDMA m_currentDMA;

        std::vector<wl_resource*> m_pendingFrameCallbacks;
        std::vector<wl_resource*> m_frameCallbacks;

        CSurface*                 m_parent = nullptr;
        std::vector<CSurface*>    m_subsurfaces;
        Hyprutils::Math::Vector2D m_subsurfaceOffset;

        bool m_foreignBuffer = false;
        bool m_mapped        = false;
    };

}
