// Desktop window of the ColibryShell compositor.
//
// CWindow binds one xdg_toplevel to its wl_surface. Windows float by
// default, live on a workspace, may be tiled, maximized or fullscreen.
// Every window owns an elastic cloth mesh (see Cloth.hpp) that gives its
// box a material feel: opens grow in with an overshoot, drags stretch the
// surface from the grab point, and automatic relocations (tiling,
// snapping, maximizing) visibly settle. Hovered titlebar button state is
// shared between the input layer and the renderer here.
//
// Windows are created exclusively through create(): the returned shared
// pointer is the single owning reference, pushed into the compositor's
// window list by the factory. The constructor is public only because
// makeShared is a free function; outside callers must use create().

#pragma once

#include "../protocol/Surface.hpp"
#include "Cloth.hpp"

#include <cstdint>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <string>
#include <vector>

namespace ColibryShell {

    class CWindow {
      public:
        // Creates the window, registers it in the compositor's window list
        // and sends the initial configure. The only supported construction
        // path.
        static Hyprutils::Memory::CSharedPointer<CWindow> create(CSurface* surface, wl_resource* toplevelResource,
                                                                 wl_resource* xdgSurfaceResource);

        // Public only for makeShared; use create(). Sends the initial
        // configure so the client may commit.
        CWindow(CSurface* surface, wl_resource* toplevelResource, wl_resource* xdgSurfaceResource);
        ~CWindow();

        CWindow(const CWindow&)            = delete;
        CWindow& operator=(const CWindow&) = delete;

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
        void openTiledFullscreen();

        bool maximized() const;
        void toggleMaximized();

        void              setAppId(const std::string& appId);
        const std::string& appId() const;

        // Starts the closing animation; the protocol close request goes
        // out when the fade reaches zero.
        void close();

        void                      setPosition(Hyprutils::Math::Vector2D pos);
        Hyprutils::Math::Vector2D clampedPosition(Hyprutils::Math::Vector2D pos) const;
        void                      setDragPosition(Hyprutils::Math::Vector2D pos);

        void moveBy(Hyprutils::Math::Vector2D delta);
        void resizeTo(Hyprutils::Math::Vector2D size);
        void setMinimized(bool minimized);

        void configure(Hyprutils::Math::Vector2D size, const std::vector<uint32_t>& states);

        void              setTitle(const std::string& title);
        const std::string& title() const;

        double openProgress() const;
        void   setOpenProgress(double progress);

        int  hoveredTitleButton() const;
        void setHoveredTitleButton(int button);

        // Cloth interaction: grab/move/release for pointer drags, nudge on
        // automatic relocations, step driven by the frame tick.
        void   clothGrab(const Hyprutils::Math::Vector2D& local);
        void   clothMoveTo(const Hyprutils::Math::Vector2D& target);
        void   clothRelease();
        void   clothNudge();
        bool   clothStep(double dtMs);
        bool   clothActive() const;
        const SClothMesh& clothMesh() const;

        // Combined rendering opacity: open fade-in times close fade-out.
        double opacity() const;

        // True once the closing fade finished and the real close request
        // was sent but the client has not unmapped yet.
        bool closeExpired(uint64_t nowMs) const;

        // Commits a drag outcome (free position or snap box); the cloth
        // keeps its deformed state and springs into the new shape.
        void applyDragCommit(const Hyprutils::Math::Vector2D& pos);
        void applySnapBox(const Hyprutils::Math::CBox& box);

      private:
        void sendConfigure(Hyprutils::Math::Vector2D size, const std::vector<uint32_t>& states);
        void sendClose();
        void onMapped();

        CSurface*    m_surface;
        wl_resource* m_toplevelResource;
        wl_resource* m_xdgSurfaceResource;

        Hyprutils::Math::Vector2D m_position;
        Hyprutils::Math::Vector2D m_size;
        int                       m_workspace = 0;
        bool                      m_mapped      = false;
        bool                      m_minimized   = false;
        bool                      m_maximized   = false;
        bool                      m_tiling      = false;
        bool                      m_fullscreen  = false;
        bool                      m_forceTiling = false;

        Hyprutils::Math::Vector2D m_prevPosition;
        Hyprutils::Math::Vector2D m_prevSize;
        bool                      m_prevTiling = false;

        double m_openProgress  = 1.0;
        int    m_hoveredButton = -1;

        bool     m_closing     = false;
        bool     m_closeSent   = false;
        double   m_closeAlpha  = 1.0;
        uint64_t m_closeSentMs = 0;

        CCloth m_cloth;

        std::string m_title;
        std::string m_appId;
    };

}
