// Linear buffer factory for the Colibry compositor.
//
// CClearRenderer owns a GBM device created from the DRM file descriptor of
// the backend and allocates output buffers with the linear modifier. Linear
// buffers are CPU-mappable, accepted by the KMS primary plane and imported
// by the backend through their dmabuf attributes.

#pragma once

#include <aquamarine/buffer/Buffer.hpp>

#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <cstdint>

namespace Colibry {

    // An Aquamarine buffer backed by a local GBM bo with the linear
    // modifier. The compositor draws into it through CPU access; the backend
    // imports its dmabuf to build the KMS framebuffer.
    class CLinearBuffer : public Aquamarine::IBuffer {
      public:
        CLinearBuffer(void* gbmDevice, Hyprutils::Math::Vector2D size);
        ~CLinearBuffer() override;

        CLinearBuffer(const CLinearBuffer&)            = delete;
        CLinearBuffer& operator=(const CLinearBuffer&) = delete;

        // Buffer kind reported to the backend; this buffer exports dmabuf.
        Aquamarine::eBufferType type();
        // Buffer capability flags; a plain linear buffer has none.
        Aquamarine::eBufferCapability caps();
        // The bo exists as long as this object does.
        bool good();
        // Maps the buffer for CPU writes; the returned tuple carries the
        // data pointer, the DRM format and the allocation size.
        std::tuple<uint8_t*, uint32_t, size_t> beginDataPtr(uint32_t flags);
        // Unmaps the buffer.
        void endDataPtr();
        // Exports the bo as dmabuf attributes with the linear modifier.
        Aquamarine::SDMABUFAttrs dmabuf();
        // Content is written synchronously over CPU; no deferred update.
        void update(const Hyprutils::Math::CRegion& damage);
        // Updates happen over CPU.
        bool isSynchronous();

      private:
        void* m_bo     = nullptr;
        void* m_mapData = nullptr;
    };

    class CClearRenderer {
      public:
        CClearRenderer()          = default;
        ~CClearRenderer()         = default;

        // Creates the GBM device from a DRM fd.
        bool init(int drmFD);

        // Releases the GBM device.
        void destroy();

        // Allocates a linear XRGB8888 buffer of the given size.
        Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer> createLinearBuffer(Hyprutils::Math::Vector2D size);

      private:
        void* m_gbmDevice = nullptr;
    };

}
