// Implementation of seat, keyboard and pointer handling.

#include "Seat.hpp"

#include "../desktop/Cursor.hpp"
#include "../desktop/DragManager.hpp"
#include "../desktop/Keybinds.hpp"
#include "../desktop/Switcher.hpp"
#include "Display.hpp"
#include "Surface.hpp"

#include "../config/ConfigManager.hpp"
#include "../core/Compositor.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include <linux/input-event-codes.h>
#include <wayland-server-protocol.h>

namespace Colibry {

    bool CSeat::init(CDisplay* display) {
        m_display = display;

        m_xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!m_xkbContext) {
            std::fprintf(stderr, "[colibry] failed to create xkb context\n");
            return false;
        }

        m_xkbKeymap = xkb_keymap_new_from_names(m_xkbContext, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!m_xkbKeymap) {
            std::fprintf(stderr, "[colibry] failed to compile the xkb keymap\n");
            return false;
        }

        const char* keymap = xkb_keymap_get_as_string(m_xkbKeymap, XKB_KEYMAP_FORMAT_TEXT_V1);
        if (!keymap) {
            std::fprintf(stderr, "[colibry] failed to serialize the xkb keymap\n");
            return false;
        }
        m_keymapString = keymap;
        std::free(const_cast<char*>(keymap));

        m_global = wl_global_create(display->waylandDisplay(), &wl_seat_interface, 9, this, &CSeat::bind);
        if (!m_global) {
            std::fprintf(stderr, "[colibry] failed to create wl_seat global\n");
            return false;
        }

        return true;
    }

    const std::string& CSeat::keymapString() const {
        return m_keymapString;
    }

    size_t CSeat::keymapSize() const {
        return m_keymapString.size() + 1;
    }

    bool CSeat::leftButtonDown() const {
        return m_leftDown;
    }

    void CSeat::onNewKeyboard(Hyprutils::Memory::CSharedPointer<Aquamarine::IKeyboard> keyboard) {
        std::fprintf(stderr, "[colibry] keyboard '%s' appeared\n", keyboard->getName().c_str());

        SKeyboard entry;
        entry.keyboard    = keyboard;
        entry.state       = xkb_state_new(m_xkbKeymap);
        entry.keyListener = keyboard->events.key.listen([this](const Aquamarine::IKeyboard::SKeyEvent& event) { onKey(event); });
        m_keyboards.push_back(std::move(entry));
    }

    void CSeat::onNewPointer(Hyprutils::Memory::CSharedPointer<Aquamarine::IPointer> pointer) {
        std::fprintf(stderr, "[colibry] pointer '%s' appeared\n", pointer->getName().c_str());

        SPointer entry;
        entry.pointer        = pointer;
        entry.moveListener   = pointer->events.move.listen([this](const Aquamarine::IPointer::SMoveEvent& event) { onPointerMove(event); });
        entry.warpListener   = pointer->events.warp.listen([this](const Aquamarine::IPointer::SWarpEvent& event) { onPointerWarp(event); });
        entry.buttonListener = pointer->events.button.listen([this](const Aquamarine::IPointer::SButtonEvent& event) { onPointerButton(event); });
        entry.axisListener   = pointer->events.axis.listen([this](const Aquamarine::IPointer::SAxisEvent& event) { onPointerAxis(event); });
        m_pointers.push_back(std::move(entry));
    }

    void CSeat::onKey(const Aquamarine::IKeyboard::SKeyEvent& event) {
        for (auto& keyboard : m_keyboards) {
            if (keyboard.state)
                xkb_state_update_key(keyboard.state, event.key + 8, event.pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
        }

        switch (event.key) {
            case KEY_LEFTCTRL:
            case KEY_RIGHTCTRL: m_ctrlPressed = event.pressed; break;
            case KEY_LEFTALT:
            case KEY_RIGHTALT: m_altPressed = event.pressed; break;
            case KEY_LEFTMETA:
            case KEY_RIGHTMETA: m_superPressed = event.pressed; break;
            case KEY_DELETE:
                if (event.pressed && m_ctrlPressed && m_altPressed) {
                    std::fprintf(stderr, "[colibry] Ctrl+Alt+Del pressed, exiting\n");
                    CCompositor::requestExit();
                }
                return;
            default: break;
        }

        // Ctrl+Alt+F1..F12 switches to another virtual terminal.
        if (event.pressed && m_ctrlPressed && m_altPressed && event.key >= KEY_F1 && event.key <= KEY_F12) {
            const auto& backend = CCompositor::get().m_backend;
            if (backend && backend->session)
                backend->session->switchVT(event.key - KEY_F1 + 1);
            return;
        }

        // The switcher overlay consumes its own keys while open.
        if (CSwitcher::get().active()) {
            if (event.pressed && event.key == KEY_TAB && m_superPressed) {
                CSwitcher::get().cycle(1);
                return;
            }
            if (event.pressed && (event.key == KEY_ENTER || event.key == KEY_KPENTER)) {
                CSwitcher::get().activate();
                return;
            }
            if (event.pressed && event.key == KEY_ESC) {
                CSwitcher::get().cancel();
                return;
            }
            if (event.pressed)
                CSwitcher::get().cancel();
        }

        uint32_t mods = 0;
        if (m_superPressed)
            mods |= BIND_SUPER;
        if (m_ctrlPressed)
            mods |= BIND_CTRL;
        if (m_altPressed)
            mods |= BIND_ALT;

        xkb_state* state = m_keyboards.empty() ? nullptr : m_keyboards.front().state;
        if (state && xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
            mods |= BIND_SHIFT;

        const xkb_keysym_t sym = state ? xkb_state_key_get_one_sym(state, event.key + 8) : XKB_KEY_NoSymbol;
        const bool consumed    = CKeybindManager::get().handle(sym, mods, event.pressed);

        if (consumed || !m_focusedSurface)
            return;

        for (const auto res : m_keyboardResources) {
            if (wl_resource_get_client(res) != wl_resource_get_client(m_focusedSurface->resource()))
                continue;

            if (state) {
                wl_keyboard_send_modifiers(res, event.timeMs, xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED),
                                           xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED),
                                           xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED),
                                           xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE));
            }

            wl_keyboard_send_key(res, event.timeMs, event.timeMs, event.key,
                                 event.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
        }
    }

    void CSeat::setKeyboardFocus(CSurface* surface) {
        if (m_focusedSurface == surface)
            return;

        const auto serial = wl_display_next_serial(CCompositor::get().m_display->waylandDisplay());

        if (m_focusedSurface) {
            for (const auto res : m_keyboardResources) {
                if (wl_resource_get_client(res) == wl_resource_get_client(m_focusedSurface->resource()))
                    wl_keyboard_send_leave(res, serial, m_focusedSurface->resource());
            }
        }

        m_focusedSurface = surface;

        if (m_focusedSurface) {
            struct wl_array keys;
            wl_array_init(&keys);

            for (const auto res : m_keyboardResources) {
                if (wl_resource_get_client(res) == wl_resource_get_client(m_focusedSurface->resource()))
                    wl_keyboard_send_enter(res, serial, m_focusedSurface->resource(), &keys);
            }

            wl_array_release(&keys);
        }
    }

    void CSeat::onPointerMove(const Aquamarine::IPointer::SMoveEvent& event) {
        auto& compositor = CCompositor::get();

        // Damage the drawn cursor at its old position, advance the real
        // position and the smoothed render position, then damage the new
        // drawn position. Only those two small boxes redraw.
        compositor.addDamage(compositor.m_cursor->damageBox());
        compositor.m_cursor->moveBy(event.delta);
        compositor.m_cursor->updateRenderPosition(event.timeMs);
        CDragManager::get().onMove(event.delta);
        compositor.addDamage(compositor.m_cursor->damageBox());

        const auto pos    = compositor.m_cursor->position();
        const auto serial = wl_display_next_serial(compositor.m_display->waylandDisplay());

        if (CConfigManager::get().followMouse() && !CDragManager::get().active())
            compositor.focusSurface(compositor.surfaceAt(pos));

        for (const auto res : m_pointerResources) {
            updatePointerFocus(res, pos, serial);
            wl_pointer_send_motion(res, event.timeMs, wl_fixed_from_double(pos.x), wl_fixed_from_double(pos.y));
        }

        compositor.requestRender();
    }

    void CSeat::onPointerWarp(const Aquamarine::IPointer::SWarpEvent& event) {
        auto& compositor = CCompositor::get();

        // Absolute positioning is a teleport: the drawn cursor snaps.
        compositor.addDamage(compositor.m_cursor->damageBox());
        compositor.m_cursor->setBounds(event.absolute);
        compositor.m_cursor->moveBy({0, 0});
        compositor.m_cursor->snapRender();
        compositor.addDamage(compositor.m_cursor->damageBox());

        const auto pos    = compositor.m_cursor->position();
        const auto serial = wl_display_next_serial(compositor.m_display->waylandDisplay());

        for (const auto res : m_pointerResources) {
            updatePointerFocus(res, pos, serial);
            wl_pointer_send_motion(res, event.timeMs, wl_fixed_from_double(pos.x), wl_fixed_from_double(pos.y));
        }

        compositor.requestRender();
    }

    void CSeat::onPointerButton(const Aquamarine::IPointer::SButtonEvent& event) {
        const auto serial = wl_display_next_serial(CCompositor::get().m_display->waylandDisplay());

        if (event.button == BTN_LEFT) {
            m_leftDown = event.pressed;

            if (event.pressed) {
                m_pressForwarded = !CDragManager::get().onPress(event.timeMs, m_superPressed);
            } else {
                CDragManager::get().onRelease();
            }
        }

        if (!m_pressForwarded)
            return;

        for (const auto res : m_pointerResources) {
            if (!m_pointerFocus.contains(res) || !m_pointerFocus[res])
                continue;

            wl_pointer_send_button(res, serial, event.timeMs, event.button,
                                   event.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        }

        if (!event.pressed)
            m_pressForwarded = false;
    }

    void CSeat::onPointerAxis(const Aquamarine::IPointer::SAxisEvent& event) {
        const uint32_t axis = static_cast<uint32_t>(event.axis) == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? WL_POINTER_AXIS_HORIZONTAL_SCROLL :
                                                                                                       WL_POINTER_AXIS_VERTICAL_SCROLL;

        for (const auto res : m_pointerResources) {
            if (!m_pointerFocus.contains(res) || !m_pointerFocus[res])
                continue;

            wl_pointer_send_axis(res, event.timeMs, axis, wl_fixed_from_double(event.delta));
        }
    }

    CSurface* CSeat::updatePointerFocus(wl_resource* pointerResource, Hyprutils::Math::Vector2D pos, uint32_t serial) {
        const auto client = wl_resource_get_client(pointerResource);
        const auto now    = CCompositor::get().surfaceAt(pos);

        CSurface* target = nullptr;
        if (now && wl_resource_get_client(now->resource()) == client)
            target = now;

        CSurface* previous = m_pointerFocus.contains(pointerResource) ? m_pointerFocus[pointerResource] : nullptr;
        if (previous == target)
            return target;

        if (previous)
            wl_pointer_send_leave(pointerResource, serial, previous->resource());

        if (target)
            wl_pointer_send_enter(pointerResource, serial, target->resource(), wl_fixed_from_double(pos.x), wl_fixed_from_double(pos.y));

        m_pointerFocus[pointerResource] = target;
        return target;
    }

    void CSeat::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
        const auto resource = wl_resource_create(client, &wl_seat_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_seat_interface IMPLEMENTATION = {
            .get_pointer  = getPointer,
            .get_keyboard = getKeyboard,
            .get_touch    = getTouch,
            .release      = [](wl_client*, wl_resource* res) { wl_resource_destroy(res); },
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, data, nullptr);

        wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
        wl_seat_send_name(resource, "seat0");
    }

    void CSeat::getKeyboard(wl_client* client, wl_resource* seat, uint32_t id) {
        auto* self = static_cast<CSeat*>(wl_resource_get_user_data(seat));

        const auto resource = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(seat), id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_keyboard_interface IMPLEMENTATION = {
            .release = [](wl_client*, wl_resource* res) { wl_resource_destroy(res); },
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, wl_resource_get_user_data(seat), [](wl_resource* res) {
            auto* self = static_cast<CSeat*>(wl_resource_get_user_data(res));
            std::erase(self->m_keyboardResources, res);
        });

        self->m_keyboardResources.push_back(resource);
        self->sendKeymap(resource);
    }

    void CSeat::sendKeymap(wl_resource* keyboardResource) {
        const auto fd = memfd_create("colibry-keymap", MFD_CLOEXEC);
        if (fd < 0)
            return;

        const auto size = keymapSize();
        if (write(fd, keymapString().c_str(), size) != (ssize_t)size) {
            close(fd);
            return;
        }
        lseek(fd, 0, SEEK_SET);

        wl_keyboard_send_keymap(keyboardResource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
        wl_keyboard_send_repeat_info(keyboardResource, 25, 600);
        close(fd);
    }

    void CSeat::getPointer(wl_client* client, wl_resource* seat, uint32_t id) {
        auto* self = static_cast<CSeat*>(wl_resource_get_user_data(seat));

        const auto resource = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(seat), id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_pointer_interface IMPLEMENTATION = {
            .set_cursor =
                [](wl_client*, wl_resource*, uint32_t, wl_resource*, int32_t, int32_t) {
                    // The compositor draws the system cursor itself; custom
                    // cursor surfaces arrive with client-decorated apps.
                },
            .release = [](wl_client*, wl_resource* res) { wl_resource_destroy(res); },
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, self, [](wl_resource* res) {
            auto* self = static_cast<CSeat*>(wl_resource_get_user_data(res));
            std::erase(self->m_pointerResources, res);
            self->m_pointerFocus.erase(res);
        });

        self->m_pointerResources.push_back(resource);
    }

    void CSeat::getTouch(wl_client* client, wl_resource* seat, uint32_t /*id*/) {
        wl_resource_post_error(seat, WL_SEAT_ERROR_MISSING_CAPABILITY, "touch capability is not available yet");
    }

}
