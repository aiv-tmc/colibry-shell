// Implementation of desktop windows.

#include "Window.hpp"

#include "../core/Compositor.hpp"
#include "../protocol/ForeignToplevel.hpp"
#include "../protocol/XdgShell.hpp"
#include "TitleBar.hpp"

#include <algorithm>
#include <cstdio>

#include <wayland-server.h>
#include <xdg-shell-server-protocol.h>

namespace Colibry {

    // Offset step for the cascade placement of new floating windows.
    static constexpr int CASCADE_STEP = 32;

    CWindow::CWindow(CSurface* surface, wl_resource* toplevelResource, wl_resource* xdgSurfaceResource) :
        m_surface(surface), m_toplevelResource(toplevelResource), m_xdgSurfaceResource(xdgSurfaceResource) {
        m_self            = Hyprutils::Memory::CSharedPointer<CWindow>(this);
        surface->onCommit = [this](CSurface* s) { onSurfaceCommit(); };
    }

    CWindow::~CWindow() {
        if (m_surface)
            m_surface->onCommit = nullptr;
    }

    void CWindow::onSurfaceCommit() {
        CXdgShell::sendInitialConfigure(m_xdgSurfaceResource);

        auto& compositor = CCompositor::get();

        if (!m_surface->mapped()) {
            if (m_mapped) {
                m_mapped = false;
                compositor.removeWindow(this);
                compositor.applyTiling();
            }
            return;
        }

        if (m_mapped) {
            // Client-driven size change: dirty the previous outer box.
            if (const auto buffer = m_surface->currentBuffer(); buffer && buffer->size() != m_size) {
                compositor.addDamage(compositor.windowBox(this));
                m_size = buffer->size();
                compositor.addDamage(compositor.windowBox(this));
            }
            return;
        }

        // First buffer: the window opens on the current workspace; a
        // workspace holding a tiled window opens new windows tiled.
        m_workspace = compositor.currentWorkspace();
        if (compositor.workspaceHasTiling(m_workspace))
            m_tiling = true;

        if (const auto buffer = m_surface->currentBuffer())
            m_size = buffer->size();

        const int cascade = compositor.m_windows.size() * CASCADE_STEP;
        m_position        = {32 + cascade, 32 + cascade};
        m_mapped          = true;

        std::fprintf(stderr, "[colibry] window mapped: '%s' at %d,%d (ws %d%s)\n", m_title.c_str(), (int)m_position.x, (int)m_position.y,
                     m_workspace, m_tiling ? ", tiled" : "");

        compositor.m_windows.push_back(m_self);
        compositor.focusWindow(this);
        compositor.applyTiling();
        compositor.addFullDamage();

        // Panels learn about the new window.
        CForeignToplevelManager::get().onWindowMapped(this);
    }

    bool CWindow::mapped() const {
        return m_mapped;
    }

    bool CWindow::visible() const {
        return m_mapped && !m_minimized && m_workspace == CCompositor::get().currentWorkspace();
    }

    bool CWindow::minimized() const {
        return m_minimized;
    }

    CSurface* CWindow::surface() const {
        return m_surface;
    }

    Hyprutils::Math::Vector2D CWindow::position() const {
        return m_position;
    }

    Hyprutils::Math::Vector2D CWindow::size() const {
        return m_size;
    }

    int CWindow::workspace() const {
        return m_workspace;
    }

    void CWindow::setWorkspace(int workspace) {
        m_workspace = workspace;
        CCompositor::get().addFullDamage();
        CForeignToplevelManager::get().onWindowState(this);
    }

    bool CWindow::isTiling() const {
        return m_tiling;
    }

    void CWindow::toggleTiling() {
        m_tiling = !m_tiling;
        auto& compositor = CCompositor::get();
        compositor.applyTiling();
        compositor.addFullDamage();
    }

    bool CWindow::isFullscreen() const {
        return m_fullscreen;
    }

    void CWindow::toggleFullscreen() {
        setFullscreen(!m_fullscreen);
    }

    void CWindow::setFullscreen(bool fullscreen) {
        if (m_fullscreen == fullscreen)
            return;

        auto& compositor = CCompositor::get();
        const auto bounds =
            compositor.m_outputs.empty() ? Hyprutils::Math::Vector2D(1920, 1080) : compositor.m_outputs.front()->size();

        compositor.addDamage(compositor.windowBox(this));

        if (fullscreen) {
            m_prevPosition = m_position;
            m_prevSize     = m_size;
            m_prevTiling   = m_tiling;
            m_tiling       = true;
            m_fullscreen   = true;
            m_maximized    = false;
            m_position     = {0, 0};
            configure(bounds, {XDG_TOPLEVEL_STATE_FULLSCREEN});
        } else {
            m_fullscreen = false;
            m_tiling     = m_prevTiling;
            m_position   = m_prevPosition;
            configure(m_prevSize, {});
        }

        compositor.applyTiling();
        compositor.addFullDamage();
        CForeignToplevelManager::get().onWindowState(this);
    }

    bool CWindow::maximized() const {
        return m_maximized;
    }

    void CWindow::toggleMaximized() {
        m_maximized = !m_maximized;

        auto& compositor = CCompositor::get();
        compositor.addDamage(compositor.windowBox(this));

        if (m_maximized) {
            const auto bounds =
                compositor.m_outputs.empty() ? Hyprutils::Math::Vector2D(1920, 1080) : compositor.m_outputs.front()->size();
            m_position = {0, 0};
            configure({bounds.x, bounds.y - TitleBar::height()}, {XDG_TOPLEVEL_STATE_MAXIMIZED});
        } else {
            const int cascade = compositor.m_windows.size() * CASCADE_STEP;
            m_position        = {32 + cascade, 32 + cascade};
            configure({800, 600}, {});
        }

        compositor.addDamage(compositor.windowBox(this));
        compositor.addDamage(Hyprutils::Math::CBox());
        compositor.requestRender();
        CForeignToplevelManager::get().onWindowState(this);
    }

    void CWindow::setAppId(const std::string& appId) {
        m_appId = appId;
        CForeignToplevelManager::get().onWindowAppId(this);
    }

    const std::string& CWindow::appId() const {
        return m_appId;
    }

    void CWindow::close() {
        if (m_toplevelResource)
            xdg_toplevel_send_close(m_toplevelResource);
    }

    void CWindow::setPosition(Hyprutils::Math::Vector2D pos) {
        if (m_fullscreen)
            return;

        auto& compositor = CCompositor::get();
        compositor.addDamage(compositor.windowBox(this));

        const auto bounds =
            compositor.m_outputs.empty() ? Hyprutils::Math::Vector2D(1920, 1080) : compositor.m_outputs.front()->size();
        m_position.x = std::clamp(pos.x, -(m_size.x - 40), bounds.x - 40.0);
        m_position.y = std::clamp(pos.y, 0.0, std::max(0.0, bounds.y - TitleBar::height() - 20.0));

        compositor.addDamage(compositor.windowBox(this));
        compositor.requestRender();
    }

    void CWindow::moveBy(Hyprutils::Math::Vector2D delta) {
        if (m_maximized || m_tiling)
            return;
        setPosition({m_position.x + delta.x, m_position.y + delta.y});
    }

    void CWindow::resizeTo(Hyprutils::Math::Vector2D size) {
        if (m_maximized || m_fullscreen)
            return;

        configure({std::max(100.0, size.x), std::max(100.0, size.y)}, {});
    }

    void CWindow::setMinimized(bool minimized) {
        if (m_minimized == minimized)
            return;

        m_minimized = minimized;
        CCompositor::get().addFullDamage();
        CForeignToplevelManager::get().onWindowState(this);
    }

    void CWindow::configure(Hyprutils::Math::Vector2D size, const std::vector<uint32_t>& states) {
        if (!m_toplevelResource)
            return;

        struct wl_array stateArray;
        wl_array_init(&stateArray);
        for (const auto state : states) {
            const auto value = static_cast<uint32_t*>(wl_array_add(&stateArray, sizeof(uint32_t)));
            *value           = state;
        }

        xdg_toplevel_send_configure(m_toplevelResource, size.x, size.y, &stateArray);
        wl_array_release(&stateArray);

        const auto serial = wl_display_next_serial(wl_client_get_display(wl_resource_get_client(m_toplevelResource)));
        xdg_surface_send_configure(m_xdgSurfaceResource, serial);
    }

    void CWindow::setTitle(const std::string& title) {
        m_title = title;
        auto& compositor = CCompositor::get();
        compositor.addDamage(
            Hyprutils::Math::CBox({m_position.x, m_position.y}, {m_size.x, (double)TitleBar::height()}));
        CForeignToplevelManager::get().onWindowTitle(this);
    }

    const std::string& CWindow::title() const {
        return m_title;
    }

}
