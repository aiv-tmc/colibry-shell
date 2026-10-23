// wl_surface implementation for the Colibry compositor.
//
// CSurface tracks the double-buffered state of one client surface: pending
// and current buffers, damage regions and frame callbacks. commit() swaps
// the pending state into the current state and notifies the desktop layer
// through a commit hook.

#pragma once

#include "Buffer.hpp"

#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <functional>
#include <vector>
#include <wayland-server.h>

namespace Colibry {

    class CDisplay;

    class CSurface {
      public:
        // wl_compositor.create_surface handler. The compositor resource user
        // data carries the owning CDisplay.
        static void create(wl_client* client, wl_resource* compositor, uint32_t id);

        // Destroys the surface resource and removes it from the display.
        void destroyResource();

        // The underlying wl_surface resource.
        wl_resource* resource() const;

        // Current buffer applied by the last commit; null when unmapped.
        Hyprutils::Memory::CSharedPointer<CBuffer> currentBuffer() const;
        // Damage accumulated since the last compositor frame, in
        // buffer-local coordinates.
        const Hyprutils::Math::CRegion& damage() const;
        // True when the surface has a committed buffer.
        bool mapped() const;

        // Sends 'done' to all pending frame callbacks and destroys them.
        void sendFrameDone(uint32_t timeMs);
        // Clears the accumulated damage after the frame was rendered.
        void clearDamage();
        // Releases the current buffer once the compositor copied its pixels.
        void releaseBuffer();

        // Called by the desktop layer (window code) for every commit.
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

        wl_resource* m_resource = nullptr;
        CDisplay*    m_display  = nullptr;

        // Owning self reference kept alive by the display list and this
        // member; reset when the resource is destroyed.
        Hyprutils::Memory::CSharedPointer<CSurface> m_self;

        // Double-buffered state.
        Hyprutils::Memory::CSharedPointer<CBuffer> m_pendingBuffer;
        Hyprutils::Memory::CSharedPointer<CBuffer> m_currentBuffer;
        Hyprutils::Math::CRegion                   m_pendingDamage;
        Hyprutils::Math::CRegion                   m_damage;

        // Frame callbacks requested since the last commit.
        std::vector<wl_resource*> m_pendingFrameCallbacks;
        std::vector<wl_resource*> m_frameCallbacks;

        bool m_mapped = false;
    };

}
