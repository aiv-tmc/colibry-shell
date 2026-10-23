// Desktop window of the Colibry compositor.
//
// CWindow binds one xdg_toplevel to its wl_surface. Windows are floating by
// construction and live on a workspace; the compositor promotes them to
// tiling (per the workspace rules) and manages tiled geometry. Fullscreen
// covers the whole output, switches the window into tiling mode while
// active and restores the previous mode and geometry on exit.

#pragma once

#include "../protocol/Surface.hpp"

#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Colibry {

    class CWindow {
      public:
        CWindow(CSurface* surface, wl_resource* toplevelResource, wl_resource* xdgSurfaceResource);
        ~CWindow();

        CWindow(const CWindow&)            = delete;
        CWindow& operator=(const CWindow&) = delete;

        // Surface commit hook: maps on the first buffer commit (applying
        // the workspace tiling rules), refreshes the size, unmaps when the
        // buffer is withdrawn.
        void onSurfaceCommit();

        bool mapped() const;
        bool visible() const;
        bool minimized() const;

        CSurface*                 surface() const;
        Hyprutils::Math::Vector2D position() const;
        Hyprutils::Math::Vector2D size() const;

        int  workspace() const;
        void setWorkspace(int workspace);
        bool isTiling() const;
        void toggleTiling();

        bool isFullscreen() const;
        void toggleFullscreen();
        void setFullscreen(bool fullscreen);

        bool maximized() const;
        void toggleMaximized();

        // Application id from xdg_toplevel.set_app_id; used by the title
        // bar icon.
        void              setAppId(const std::string& appId);
        const std::string& appId() const;

        void close();
        void setPosition(Hyprutils::Math::Vector2D pos);
        void moveBy(Hyprutils::Math::Vector2D delta);
        void resizeTo(Hyprutils::Math::Vector2D size);
        void setMinimized(bool minimized);

        // Sends a toplevel configure with the given size and state values
        // (raw XDG_TOPLEVEL_STATE_* enum values).
        void configure(Hyprutils::Math::Vector2D size, const std::vector<uint32_t>& states);

        void              setTitle(const std::string& title);
        const std::string& title() const;

      private:
        CSurface*    m_surface;
        wl_resource* m_toplevelResource;
        wl_resource* m_xdgSurfaceResource;

        Hyprutils::Memory::CSharedPointer<CWindow> m_self;

        Hyprutils::Math::Vector2D m_position;
        Hyprutils::Math::Vector2D m_size;
        int                       m_workspace = 1;
        bool                      m_mapped      = false;
        bool                      m_minimized   = false;
        bool                      m_maximized   = false;
        bool                      m_tiling      = false;
        bool                      m_fullscreen  = false;

        Hyprutils::Math::Vector2D m_prevPosition;
        Hyprutils::Math::Vector2D m_prevSize;
        bool                      m_prevTiling = false;

        std::string m_title;
        std::string m_appId;
    };

}
