// Implementation of the compositor seat: wl_seat global, keyboard focus
// and pointer focus, keybind dispatch.

#include "Seat.hpp"

#include "../core/Compositor.hpp"
#include "../desktop/Cursor.hpp"
#include "../desktop/DragManager.hpp"
#include "../desktop/Keybinds.hpp"
#include "../desktop/Launcher.hpp"
#include "../desktop/Switcher.hpp"
#include "../desktop/Window.hpp"
#include "Display.hpp"
#include "Surface.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/input-event-codes.h>
#include <linux/vt.h>

#include <xkbcommon/xkbcommon.h>

namespace ColibryShell {

    // Startup fade guard shared by every input handler.
    static bool inputLocked() {
        return CCompositor::get().inputLocked();
    }

    bool CSeat::init(CDisplay* display) {
        m_display = display;

        m_xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!m_xkbContext) {
            std::fprintf(stderr, "[colibry-shell] failed to create the xkb context\n");
            return false;
        }

        m_xkbKeymap = xkb_keymap_new_from_names(m_xkbContext, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!m_xkbKeymap) {
            std::fprintf(stderr, "[colibry-shell] failed to compile the xkb keymap\n");
            return false;
        }

        // The 2-argument variant returns a newly allocated string.
        const char* keymap = xkb_keymap_get_as_string(m_xkbKeymap, XKB_KEYMAP_FORMAT_TEXT_V1);
        if (!keymap) {
            std::fprintf(stderr, "[colibry-shell] failed to serialize the xkb keymap\n");
            return false;
        }
        m_keymapString = keymap;
        std::free(const_cast<char*>(keymap));

        m_global = wl_global_create(display->waylandDisplay(), &wl_seat_interface, 9, this, &CSeat::bind);
        if (!m_global) {
            std::fprintf(stderr, "[colibry-shell] failed to create the wl_seat global\n");
            return false;
        }

        std::fprintf(stderr, "[colibry-shell] seat ready\n");
        return true;
    }

    const std::string& CSeat::keymapString() const { return m_keymapString; }
    size_t            CSeat::keymapSize() const { return m_keymapString.size(); }
    bool              CSeat::leftButtonDown() const { return m_leftDown; }

    void CSeat::onNewKeyboard(Hyprutils::Memory::CSharedPointer<Aquamarine::IKeyboard> keyboard) {
        std::fprintf(stderr, "[colibry-shell] keyboard '%s' appeared\n", keyboard->getName().c_str());

        SKeyboard entry;
        entry.keyboard    = keyboard;
        entry.state       = xkb_state_new(m_xkbKeymap);
        entry.keyListener = keyboard->events.key.listen([this](const Aquamarine::IKeyboard::SKeyEvent& event) { onKey(event); });

        m_keyboards.push_back(std::move(entry));
    }

    void CSeat::onNewPointer(Hyprutils::Memory::CSharedPointer<Aquamarine::IPointer> pointer) {
        std::fprintf(stderr, "[colibry-shell] pointer '%s' appeared\n", pointer->getName().c_str());

        SPointer entry;
        entry.pointer        = pointer;
        entry.moveListener   = pointer->events.move.listen([this](const Aquamarine::IPointer::SMoveEvent& event) { onPointerMove(event); });
        entry.warpListener   = pointer->events.warp.listen([this](const Aquamarine::IPointer::SWarpEvent& event) { onPointerWarp(event); });
        entry.buttonListener = pointer->events.button.listen([this](const Aquamarine::IPointer::SButtonEvent& event) { onPointerButton(event); });
        entry.axisListener   = pointer->events.axis.listen([this](const Aquamarine::IPointer::SAxisEvent& event) { onPointerAxis(event); });

        m_pointers.push_back(std::move(entry));
    }

    void CSeat::setKeyboardFocus(CSurface* surface) {
        if (m_focusedSurface == surface)
            return;

        const auto display = m_display->waylandDisplay();
        const auto serial  = wl_display_next_serial(display);

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

    void CSeat::onKey(const Aquamarine::IKeyboard::SKeyEvent& event) {
        auto& compositor = CCompositor::get();

        static bool s_firstKeyLogged = false;
        if (!s_firstKeyLogged) {
            s_firstKeyLogged = true;
            std::fprintf(stderr, "[colibry-shell] first key event: key=%u pressed=%d\n", event.key, (int)event.pressed);
        }

        // Emergency exit, independent of everything else: raw Ctrl+Alt with
        // Delete or Backspace leaves the session even if xkb, the bind
        // table or focus handling are broken.
        if (event.pressed && m_ctrlPressed && m_altPressed && (event.key == KEY_DELETE || event.key == KEY_BACKSPACE)) {
            CCompositor::requestExit();
            return;
        }

        if (inputLocked())
            return;

        if (event.key == KEY_LEFTCTRL || event.key == KEY_RIGHTCTRL)
            m_ctrlPressed = event.pressed;
        if (event.key == KEY_LEFTALT || event.key == KEY_RIGHTALT)
            m_altPressed = event.pressed;
        if (event.key == KEY_LEFTMETA || event.key == KEY_RIGHTMETA)
            m_superPressed = event.pressed;

        for (auto& entry : m_keyboards)
            xkb_state_update_key(entry.state, event.key + 8, event.pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

        auto* primary = m_keyboards.empty() ? nullptr : m_keyboards.front().state;
        if (!primary)
            return;

        uint32_t mods = 0;
        if (xkb_state_mod_name_is_active(primary, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
            mods |= BIND_SUPER;
        if (xkb_state_mod_name_is_active(primary, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
            mods |= BIND_CTRL;
        if (xkb_state_mod_name_is_active(primary, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
            mods |= BIND_ALT;
        if (xkb_state_mod_name_is_active(primary, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
            mods |= BIND_SHIFT;

        const xkb_keysym_t sym = xkb_state_key_get_one_sym(primary, event.key + 8);

        // Ctrl+Alt+F1..F12 switches the virtual terminal.
        if (m_ctrlPressed && m_altPressed && sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
            if (event.pressed) {
                const auto tty = open("/dev/tty0", O_RDWR);
                if (tty >= 0) {
                    ioctl(tty, VT_ACTIVATE, (int)(sym - XKB_KEY_F1 + 1));
                    close(tty);
                }
            }
            return;
        }

        if (CLauncher::get().active()) {
            CLauncher::get().onKey(sym, mods, event.pressed);
            return;
        }

        if (CSwitcher::get().active()) {
            if (event.pressed) {
                if (sym == XKB_KEY_Tab || sym == XKB_KEY_Right || sym == XKB_KEY_Down)
                    CSwitcher::get().cycle(1);
                else if (sym == XKB_KEY_Left || sym == XKB_KEY_Up)
                    CSwitcher::get().cycle(-1);
                else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter)
                    CSwitcher::get().activate();
                else if (sym == XKB_KEY_Escape)
                    CSwitcher::get().cancel();
            }
            return;
        }

        if (CKeybindManager::get().handle(sym, mods, event.pressed))
            return;

        if (!m_focusedSurface)
            return;

        const auto serial = wl_display_next_serial(m_display->waylandDisplay());
        const auto client = wl_resource_get_client(m_focusedSurface->resource());

        for (const auto res : m_keyboardResources) {
            if (wl_resource_get_client(res) == client)
                wl_keyboard_send_key(res, serial, event.timeMs, event.key, event.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
        }
    }

    void CSeat::onPointerMove(const Aquamarine::IPointer::SMoveEvent& event) {
        auto& compositor = CCompositor::get();
        if (inputLocked())
            return;

        compositor.addDamage(compositor.m_cursor->damageBox());
        CDragManager::get().onMove(event.delta);
        compositor.addDamage(compositor.m_cursor->damageBox());

        const auto pos    = compositor.m_cursor->position();
        const auto serial = wl_display_next_serial(compositor.m_display->waylandDisplay());

        for (const auto res : m_pointerResources) {
            updatePointerFocus(res, pos, serial);
            wl_pointer_send_motion(res, event.timeMs, wl_fixed_from_double(pos.x), wl_fixed_from_double(pos.y));
        }

        compositor.requestRender();
    }

    void CSeat::onPointerWarp(const Aquamarine::IPointer::SWarpEvent& event) {
        auto& compositor = CCompositor::get();
        if (inputLocked())
            return;

        // Some absolute devices (and broken VMs) emit coordinates outside
        // the screen; clamping keeps the cursor on the desktop instead of
        // letting it "mirror" through the opposite edge.
        const auto& outputs = compositor.m_outputs;
        const auto  bounds  = outputs.empty() ? Hyprutils::Math::Vector2D(1920, 1080) : outputs.front()->size();

        Hyprutils::Math::Vector2D pos(
            std::clamp(event.absolute.x, 0.0, std::max(0.0, bounds.x - 1.0)),
            std::clamp(event.absolute.y, 0.0, std::max(0.0, bounds.y - 1.0)));

        static bool s_firstWarpLogged = false;
        if (!s_firstWarpLogged) {
            s_firstWarpLogged = true;
            std::fprintf(stderr, "[colibry-shell] first absolute warp: %.1f,%.1f (bounds %.0fx%.0f)\n", event.absolute.x, event.absolute.y, bounds.x,
                         bounds.y);
        }

        compositor.addDamage(compositor.m_cursor->damageBox());
        compositor.m_cursor->setPosition(pos);
        compositor.m_cursor->snapRender();
        compositor.addDamage(compositor.m_cursor->damageBox());

        const auto serial = wl_display_next_serial(compositor.m_display->waylandDisplay());
        for (const auto res : m_pointerResources) {
            updatePointerFocus(res, pos, serial);
            wl_pointer_send_motion(res, event.timeMs, wl_fixed_from_double(pos.x), wl_fixed_from_double(pos.y));
        }

        compositor.requestRender();
    }

    void CSeat::onPointerButton(const Aquamarine::IPointer::SButtonEvent& event) {
        auto& compositor = CCompositor::get();
        if (inputLocked())
            return;

        const auto serial = wl_display_next_serial(compositor.m_display->waylandDisplay());

        if (event.button == BTN_LEFT) {
            m_leftDown = event.pressed;

            if (event.pressed)
                m_pressForwarded = !CDragManager::get().onPress(event.timeMs, m_superPressed);
            else
                CDragManager::get().onRelease();
        }

        if (!m_pressForwarded)
            return;

        for (const auto res : m_pointerResources) {
            if (!m_pointerFocus.contains(res) || !m_pointerFocus[res])
                continue;

            wl_pointer_send_button(res, serial, event.timeMs, event.button, event.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        }

        if (!event.pressed)
            m_pressForwarded = false;
    }

    void CSeat::onPointerAxis(const Aquamarine::IPointer::SAxisEvent& event) {
        auto& compositor = CCompositor::get();
        if (inputLocked())
            return;

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

        wl_resource_set_implementation(resource, &IMPLEMENTATION, self, [](wl_resource* res) {
            auto* self = static_cast<CSeat*>(wl_resource_get_user_data(res));
            std::erase(self->m_keyboardResources, res);
        });

        self->m_keyboardResources.push_back(resource);
        self->sendKeymap(resource);
    }

    void CSeat::sendKeymap(wl_resource* keyboardResource) {
        const auto fd = memfd_create("colibry-shell-keymap", MFD_CLOEXEC);
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
                    // The compositor draws the system cursor itself.
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
        wl_resource_post_error(seat, WL_SEAT_ERROR_MISSING_CAPABILITY, "touch capability is not available");
    }

}
