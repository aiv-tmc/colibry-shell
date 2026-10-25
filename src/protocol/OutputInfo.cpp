// Implementation of the wl_output globals.

#include "OutputInfo.hpp"

#include "Display.hpp"

#include <algorithm>
#include <cstdio>

#include <wayland-server-protocol.h>

namespace Colibry {

    COutputInfo& COutputInfo::get() {
        static COutputInfo s_instance;
        return s_instance;
    }

    bool COutputInfo::init(CDisplay* display) {
        m_display = display;
        return true;
    }

    void COutputInfo::addOutput(const std::string& name, Hyprutils::Math::Vector2D size) {
        auto* output   = new SOutput();
        output->name   = name;
        output->size   = size;
        output->global = wl_global_create(m_display->waylandDisplay(), &wl_output_interface, 4, output, &COutputInfo::bind);
        if (!output->global) {
            std::fprintf(stderr, "[colibry] failed to create wl_output global for %s\n", name.c_str());
            delete output;
            return;
        }

        m_outputs.push_back(output);
    }

    void COutputInfo::removeOutput(const std::string& name) {
        for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it) {
            if ((*it)->name == name) {
                wl_global_destroy((*it)->global);
                delete *it;
                m_outputs.erase(it);
                return;
            }
        }
    }

    wl_resource* COutputInfo::resourceFor(wl_client* client, const std::string& name) {
        for (const auto* output : m_outputs) {
            if (output->name != name)
                continue;

            for (auto* resource : output->resources) {
                if (wl_resource_get_client(resource) == client)
                    return resource;
            }
        }

        return nullptr;
    }

    void COutputInfo::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
        std::fprintf(stderr, "[colibry] client bound wl_output\n");

        const auto output = static_cast<SOutput*>(data);

        const auto resource = wl_resource_create(client, &wl_output_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_output_interface IMPLEMENTATION = {
            .release = release,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, output, [](wl_resource* res) {
            auto* output = static_cast<SOutput*>(wl_resource_get_user_data(res));
            std::erase(output->resources, res);
        });

        wl_output_send_geometry(resource, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN, "colibry", output->name.c_str(), WL_OUTPUT_TRANSFORM_NORMAL);
        wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, (int32_t)output->size.x, (int32_t)output->size.y, 60000);

        if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
            wl_output_send_scale(resource, 1);
        if (version >= WL_OUTPUT_NAME_SINCE_VERSION)
            wl_output_send_name(resource, output->name.c_str());
        if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
            wl_output_send_done(resource);

        output->resources.push_back(resource);
    }

    void COutputInfo::release(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

}
