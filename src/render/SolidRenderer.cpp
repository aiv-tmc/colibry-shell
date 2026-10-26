// Implementation of the linear buffer factory.

#include "SolidRenderer.hpp"

#include <gbm.h>

#include <cstdio>

#include <drm_fourcc.h>

namespace ColibryShell {

    CLinearBuffer::CLinearBuffer(void* gbmDevice, Hyprutils::Math::Vector2D size) {
        this->size = size;

        // Allocates the buffer with the linear modifier: CPU-mappable,
        // importable by EGL and always accepted by the KMS primary plane.
        const uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
        m_bo                    = gbm_bo_create_with_modifiers(static_cast<gbm_device*>(gbmDevice), size.x, size.y, GBM_FORMAT_XRGB8888, &modifier, 1);
    }

    CLinearBuffer::~CLinearBuffer() {
        if (m_bo)
            gbm_bo_destroy(static_cast<gbm_bo*>(m_bo));
    }

    Aquamarine::eBufferType CLinearBuffer::type() {
        return Aquamarine::BUFFER_TYPE_DMABUF;
    }

    Aquamarine::eBufferCapability CLinearBuffer::caps() {
        return static_cast<Aquamarine::eBufferCapability>(0);
    }

    bool CLinearBuffer::good() {
        return m_bo != nullptr;
    }

    std::tuple<uint8_t*, uint32_t, size_t> CLinearBuffer::beginDataPtr(uint32_t /*flags*/) {
        if (!m_bo)
            return {nullptr, 0, 0};

        uint32_t stride = 0;
        void*    ptr    = gbm_bo_map(static_cast<gbm_bo*>(m_bo), 0, 0, size.x, size.y, GBM_BO_TRANSFER_WRITE, &stride, &m_mapData);
        if (!ptr)
            return {nullptr, 0, 0};

        return {static_cast<uint8_t*>(ptr), DRM_FORMAT_XRGB8888, (size_t)stride * size.y};
    }

    void CLinearBuffer::endDataPtr() {
        if (m_bo && m_mapData) {
            gbm_bo_unmap(static_cast<gbm_bo*>(m_bo), m_mapData);
            m_mapData = nullptr;
        }
    }

    Aquamarine::SDMABUFAttrs CLinearBuffer::dmabuf() {
        Aquamarine::SDMABUFAttrs attrs = {};

        if (!m_bo) {
            attrs.success = false;
            return attrs;
        }

        attrs.success    = true;
        attrs.planes     = 1;
        attrs.size       = size;
        attrs.format     = DRM_FORMAT_XRGB8888;
        attrs.modifier   = DRM_FORMAT_MOD_LINEAR;
        attrs.fds[0]     = gbm_bo_get_fd(static_cast<gbm_bo*>(m_bo));
        attrs.offsets[0] = gbm_bo_get_offset(static_cast<gbm_bo*>(m_bo), 0);
        attrs.strides[0] = gbm_bo_get_stride(static_cast<gbm_bo*>(m_bo));

        if (attrs.fds[0] < 0)
            attrs.success = false;

        return attrs;
    }

    void CLinearBuffer::update(const Hyprutils::Math::CRegion& /*damage*/) {
        // The buffer content is written synchronously through CPU access;
        // there is no deferred state to update.
    }

    bool CLinearBuffer::isSynchronous() {
        return true;
    }

    bool CClearRenderer::init(int drmFD) {
        // Wraps the DRM fd in a GBM device.
        m_gbmDevice = gbm_create_device(drmFD);
        if (!m_gbmDevice) {
            std::fprintf(stderr, "[colibry-shell] renderer: failed to create GBM device\n");
            return false;
        }

        std::fprintf(stderr, "[colibry-shell] renderer: GBM device ready\n");
        return true;
    }

    void CClearRenderer::destroy() {
        if (m_gbmDevice)
            gbm_device_destroy(static_cast<gbm_device*>(m_gbmDevice));
        m_gbmDevice = nullptr;
    }

    Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer> CClearRenderer::createLinearBuffer(Hyprutils::Math::Vector2D size) {
        if (!m_gbmDevice)
            return Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>(nullptr);

        auto* buffer = new CLinearBuffer(m_gbmDevice, size);
        if (!buffer->good()) {
            delete buffer;
            return Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>(nullptr);
        }

        return Hyprutils::Memory::CSharedPointer<Aquamarine::IBuffer>(buffer);
    }

}
