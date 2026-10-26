// Seat, keyboard and pointer handling for the Colibry compositor.
//
// CSeat announces the wl_seat global with keyboard and pointer capabilities,
// builds the xkbcommon keymap, maintains the keyboard focus, forwards key
// events to the focused surface and pointer events to pointer resources.
// Window manipulation (dragging, resizing, title bar buttons) is delegated
// to the drag manager on button presses.

#pragma once

#include <aquamarine/input/Input.hpp>

#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/signal/Signal.hpp>

#include <map>
#include <string>
#include <vector>
#include <wayland-server.h>

#include <xkbcommon/xkbcommon.h>

namespace ColibryShell {

    class CDisplay;
    class CSurface;

    class CSeat {
      public:
        CSeat()          = default;
        ~CSeat()         = default;

        bool init(CDisplay* display);

        void onNewKeyboard(Hyprutils::Memory::CSharedPointer<Aquamarine::IKeyboard> keyboard);
        void onNewPointer(Hyprutils::Memory::CSharedPointer<Aquamarine::IPointer> pointer);

        // Moves the keyboard focus to the given surface; keyboard enter and
        // leave are sent to the affected clients.
        void setKeyboardFocus(CSurface* surface);

        // True while the left mouse button is held; drives the title bar
        // press state.
        bool leftButtonDown() const;

        const std::string& keymapString() const;
        size_t             keymapSize() const;

      private:
        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);
        static void getKeyboard(wl_client* client, wl_resource* seat, uint32_t id);
        static void getPointer(wl_client* client, wl_resource* seat, uint32_t id);
        static void getTouch(wl_client* client, wl_resource* seat, uint32_t id);

        void onKey(const Aquamarine::IKeyboard::SKeyEvent& event);
        void onPointerMove(const Aquamarine::IPointer::SMoveEvent& event);
        void onPointerWarp(const Aquamarine::IPointer::SWarpEvent& event);
        void onPointerButton(const Aquamarine::IPointer::SButtonEvent& event);
        void onPointerAxis(const Aquamarine::IPointer::SAxisEvent& event);

        void sendKeymap(wl_resource* keyboardResource);

        CSurface* updatePointerFocus(wl_resource* pointerResource, Hyprutils::Math::Vector2D pos, uint32_t serial);

        CDisplay*  m_display = nullptr;
        wl_global* m_global  = nullptr;

        xkb_context* m_xkbContext = nullptr;
        xkb_keymap*  m_xkbKeymap  = nullptr;
        std::string  m_keymapString;

        // Surface owning the keyboard focus.
        CSurface* m_focusedSurface = nullptr;

        struct SKeyboard {
            Hyprutils::Memory::CSharedPointer<Aquamarine::IKeyboard> keyboard;
            Hyprutils::Signal::CHyprSignalListener                   keyListener;
            xkb_state*                                             state = nullptr;
        };
        std::vector<SKeyboard> m_keyboards;

        struct SPointer {
            Hyprutils::Memory::CSharedPointer<Aquamarine::IPointer> pointer;
            Hyprutils::Signal::CHyprSignalListener                  moveListener;
            Hyprutils::Signal::CHyprSignalListener                  warpListener;
            Hyprutils::Signal::CHyprSignalListener                  buttonListener;
            Hyprutils::Signal::CHyprSignalListener                  axisListener;
        };
        std::vector<SPointer> m_pointers;

        std::vector<wl_resource*> m_keyboardResources;
        std::vector<wl_resource*> m_pointerResources;

        std::map<wl_resource*, CSurface*> m_pointerFocus;

        bool m_pressForwarded = false;
        bool m_leftDown       = false;

        bool m_ctrlPressed  = false;
        bool m_altPressed   = false;
        bool m_superPressed = false;
    };

}
