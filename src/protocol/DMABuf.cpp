// Implementation of linux-dmabuf (single-plane, ARGB8888/XRGB8888 only).

#include "DMABuf.hpp"

#include "Display.hpp"

#include <cstdio>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux-dmabuf-unstable-v1-server-protocol.h>

namespace ColibryShell {

    // Accumulated state of one in-flight zwp_linux_buffer_params object.
    struct SParams {
        bool     hasPlane = false;
        int32_t  fd       = -1;
        uint32_t offset   = 0;
        uint32_t stride   = 0;
        uint64_t modifier = 0;
    };

    // Frees the dmabuf fd together with the wl_buffer resource.
    static void bufferDestroyCallback(wl_resource* res) {
        auto* attrs = static_cast<SDMABufAttrs*>(wl_resource_get_user_data(res));
        if (attrs) {
            if (attrs->fd >= 0)
                close(attrs->fd);
            delete attrs;
        }
    }

    CDMABufManager& CDMABufManager::get() {
        static CDMABufManager s_instance;
        return s_instance;
    }

    bool CDMABufManager::init(CDisplay* display) {
        m_display = display;

        m_global = wl_global_create(display->waylandDisplay(), &zwp_linux_dmabuf_v1_interface, 3, this, &CDMABufManager::bind);
        if (!m_global) {
            std::fprintf(stderr, "[colibry-shell] failed to create the linux-dmabuf global\n");
            return false;
        }

        return true;
    }

    void CDMABufManager::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
        const auto resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct zwp_linux_dmabuf_v1_interface IMPLEMENTATION = {
            .destroy       = destroy,
            .create_params = createParams,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, data, nullptr);

        // Advertise the two formats the renderer imports, each with the
        // explicit linear modifier and the implicit-modifier wildcard
        // (DRM_FORMAT_MOD_INVALID); Mesa refuses to allocate otherwise on
        // most hardware.
        zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_ARGB8888);
        zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_XRGB8888);

        if (version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
            zwp_linux_dmabuf_v1_send_modifier(resource, DRM_FORMAT_ARGB8888, 0, 0);
            zwp_linux_dmabuf_v1_send_modifier(resource, DRM_FORMAT_XRGB8888, 0, 0);
            zwp_linux_dmabuf_v1_send_modifier(resource, DRM_FORMAT_ARGB8888, 0xFFFFFFFF, 0xFFFFFFFF);
            zwp_linux_dmabuf_v1_send_modifier(resource, DRM_FORMAT_XRGB8888, 0xFFFFFFFF, 0xFFFFFFFF);
        }
    }

    void CDMABufManager::destroy(wl_client*, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CDMABufManager::createParams(wl_client* client, wl_resource*, uint32_t id) {
        const auto resource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, 1, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct zwp_linux_buffer_params_v1_interface IMPLEMENTATION = {
            .destroy      = paramsDestroy,
            .add          = paramsAdd,
            .create       = paramsCreate,
            .create_immed = paramsCreateImmed,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, new SParams(),
                                       [](wl_resource* res) { delete static_cast<SParams*>(wl_resource_get_user_data(res)); });
    }

    void CDMABufManager::paramsDestroy(wl_client*, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CDMABufManager::paramsAdd(wl_client* client, wl_resource* resource, int32_t fd, uint32_t planeIdx, uint32_t offset, uint32_t stride,
                                   uint32_t modifierHi, uint32_t modifierLo) {
        auto* params = static_cast<SParams*>(wl_resource_get_user_data(resource));

        if (params->hasPlane || planeIdx != 0) {
            wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET, "only plane 0 is supported");
            close(fd);
            return;
        }

        if (stride == 0) {
            wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "invalid stride");
            close(fd);
            return;
        }

        params->hasPlane = true;
        params->fd       = fd;
        params->offset   = offset;
        params->stride   = stride;
        params->modifier = ((uint64_t)modifierHi << 32) | modifierLo;
        (void)client;
    }

    // Shared tail of create and create_immed: builds the wl_buffer from the
    // collected params. Returns the buffer resource, or nullptr after
    // posting an error to the params resource.
    static wl_resource* buildBuffer(wl_client* client, wl_resource* paramsResource, uint32_t bufferId, int32_t width, int32_t height,
                                    uint32_t format) {
        auto* params = static_cast<SParams*>(wl_resource_get_user_data(paramsResource));

        if (!params->hasPlane) {
            wl_resource_post_error(paramsResource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "no plane added");
            return nullptr;
        }

        if (format != DRM_FORMAT_ARGB8888 && format != DRM_FORMAT_XRGB8888) {
            wl_resource_post_error(paramsResource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "unsupported format");
            return nullptr;
        }

        if (width <= 0 || height <= 0) {
            wl_resource_post_error(paramsResource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "invalid size");
            return nullptr;
        }

        const auto buffer = wl_resource_create(client, &wl_buffer_interface, 1, bufferId);
        if (!buffer) {
            wl_client_post_no_memory(client);
            return nullptr;
        }

        auto* attrs     = new SDMABufAttrs();
        attrs->valid    = true;
        attrs->fd       = params->fd;
        attrs->width    = (uint32_t)width;
        attrs->height   = (uint32_t)height;
        attrs->format   = format;
        attrs->modifier = params->modifier;
        attrs->stride   = params->stride;
        attrs->offset   = params->offset;

        // Ownership of the fd moves to the buffer resource.
        params->fd       = -1;
        params->hasPlane = false;

        wl_resource_set_implementation(buffer, nullptr, attrs, &bufferDestroyCallback);
        return buffer;
    }

    void CDMABufManager::paramsCreate(wl_client* client, wl_resource* resource, int32_t width, int32_t height, uint32_t format,
                                      uint32_t flags) {
        (void)flags;

        const auto buffer = buildBuffer(client, resource, 0, width, height, format);
        if (!buffer) {
            wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "buffer creation failed");
            return;
        }

        zwp_linux_buffer_params_v1_send_created(resource, buffer);
    }

    void CDMABufManager::paramsCreateImmed(wl_client* client, wl_resource* resource, uint32_t bufferId, int32_t width, int32_t height,
                                           uint32_t format, uint32_t flags) {
        (void)flags;
        buildBuffer(client, resource, bufferId, width, height, format);
    }

}
