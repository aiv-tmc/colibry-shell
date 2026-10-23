// wl_output globals for the Colibry compositor.
//
// COutputInfo announces one wl_output global per backend output so panels
// and bars can discover displays, and answers per-client resource lookups
// (used by foreign-toplevel output_enter events). Only the static output
// description is implemented: geometry, one mode, scale and name.

#pragma once

#include <hyprutils/math/Vector2D.hpp>

#include <string>
#include <vector>
#include <wayland-server.h>

namespace Colibry {

    class CDisplay;

    class COutputInfo {
      public:
        static COutputInfo& get();

        // Creates no globals yet; outputs are announced through addOutput.
        bool init(CDisplay* display);

        // Announces an output global with the given name and pixel size.
        void addOutput(const std::string& name, Hyprutils::Math::Vector2D size);
        // Destroys the global of the named output.
        void removeOutput(const std::string& name);

        // Returns the wl_output resource the given client holds for the
        // named output, or nullptr.
        wl_resource* resourceFor(wl_client* client, const std::string& name);

      private:
        COutputInfo()  = default;
        ~COutputInfo() = default;

        COutputInfo(const COutputInfo&)            = delete;
        COutputInfo& operator=(const COutputInfo&) = delete;

        // One announced output; resources holds the per-client bindings.
        struct SOutput {
            std::string               name;
            Hyprutils::Math::Vector2D size;
            wl_global*                global = nullptr;
            std::vector<wl_resource*> resources;
        };

        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);
        static void release(wl_client* client, wl_resource* resource);

        CDisplay* m_display = nullptr;

        std::vector<SOutput*> m_outputs;
    };

}
