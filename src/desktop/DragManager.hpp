// Interactive move, resize and release-spring state for the Colibry
// compositor.
//
// One drag is active at a time. A move or resize starts from a titlebar or
// edge hit (see ChromeGeom) and follows the cursor with rubber-band
// resistance past the screen edges; releasing a move starts an underdamped
// spring that settles the window into place. The rendered cursor stays
// pinned at the grab point for the whole grab.

#pragma once

#include <chrono>
#include <cstdint>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

namespace Colibry {

    class CWindow;

    class CDragManager {
      public:
        static CDragManager& get();

        bool active() const;

        // Fills `out` with the snap preview rectangle (half/quarter tiling
        // zones) while a move drag hovers near a screen edge.
        bool snapPreview(Hyprutils::Math::CBox& out) const;

        // Starts a grab from the current cursor position. Returns true when
        // the press was consumed by chrome (button, drag or resize).
        bool onPress(uint32_t timeMs, bool superHeld);

        // Feeds relative pointer motion into the active grab.
        void onMove(Hyprutils::Math::Vector2D delta);

        // Ends the grab; a move springs toward its final position.
        void onRelease();

        // Advances release springs by the given frame delta.
        void stepSprings(double dtMs);

      private:
        CDragManager()          = default;
        ~CDragManager()         = default;

        CDragManager(const CDragManager&)            = delete;
        CDragManager& operator=(const CDragManager&) = delete;

        static CWindow* windowAt(Hyprutils::Math::Vector2D pos);
        static bool     transparentAt(CWindow* window, Hyprutils::Math::Vector2D pos);

        void updateSnap(const Hyprutils::Math::Vector2D& cursor, const Hyprutils::Math::Vector2D& bounds);
        void applyMove(const Hyprutils::Math::Vector2D& pos);
        void applyResize(const Hyprutils::Math::Vector2D& cursor);
        void startSpring(const Hyprutils::Math::Vector2D& target);
        bool springStep(double dtMs);

        CWindow* m_window = nullptr;

        // 0 = none, 1 = move, 2 = resize.
        int m_mode  = 0;
        int m_edges = 0;

        Hyprutils::Math::Vector2D m_startCursor = {0, 0};
        Hyprutils::Math::Vector2D m_startPos    = {0, 0};
        Hyprutils::Math::Vector2D m_startSize   = {0, 0};
        Hyprutils::Math::Vector2D m_grabOffset  = {0, 0};

        bool                 m_hasSnap  = false;
        Hyprutils::Math::CBox m_snapRect;

        CWindow*                       m_lastClickWindow = nullptr;
        std::chrono::steady_clock::time_point m_lastClickTime;

        // Release spring state.
        Hyprutils::Math::Vector2D m_springTarget = {0, 0};
        Hyprutils::Math::Vector2D m_springPos    = {0, 0};
        Hyprutils::Math::Vector2D m_springVel    = {0, 0};
        bool                      m_springActive = false;
    };

}
