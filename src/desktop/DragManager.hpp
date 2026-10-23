// Interactive move and resize for Colibry windows.
//
// CDragManager implements the pointer-driven window manipulation: dragging
// by the title bar, by fully transparent pixels of the client content, by
// the window borders, and — while SUPER is held — by any client content.
// Double click on the title bar toggles maximization; window buttons
// execute on press.

#pragma once

#include <aquamarine/input/Input.hpp>

#include <hyprutils/math/Vector2D.hpp>

#include <chrono>

namespace Colibry {

    class CWindow;

    class CDragManager {
      public:
        static CDragManager& get();

        // Handles a left button press at the current cursor position;
        // superHeld mirrors the live SUPER modifier state. Returns true
        // when the press was consumed by the compositor.
        bool onPress(uint32_t timeMs, bool superHeld);

        // Applies the drag for a cursor move.
        void onMove(Hyprutils::Math::Vector2D delta);

        // Ends any active drag.
        void onRelease();

        // True while a move or resize drag is in progress.
        bool active() const;

      private:
        CDragManager()  = default;
        ~CDragManager() = default;

        // Returns the topmost visible window containing the position.
        CWindow* windowAt(Hyprutils::Math::Vector2D pos);

        // True when the content pixel under pos is fully transparent.
        bool transparentAt(CWindow* window, Hyprutils::Math::Vector2D pos);

        CWindow* m_window = nullptr;

        // Active drag: mode 0 none, 1 move, 2 resize.
        int m_mode = 0;
        // Resize edge bitmask (TitleBar hit values).
        uint32_t m_edges = 0;

        Hyprutils::Math::Vector2D m_startCursor;
        Hyprutils::Math::Vector2D m_startPos;
        Hyprutils::Math::Vector2D m_startSize;

        // Last title bar press for double click detection.
        CWindow*                              m_lastClickWindow = nullptr;
        std::chrono::steady_clock::time_point m_lastClickTime{};
    };

}
