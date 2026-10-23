// Keybind handling for the Colibry compositor.
//
// CKeybindManager matches key events (modifiers + xkb keysym) against the
// default binds and the user binds from the TOML configuration and executes
// the bound actions: spawning programs, closing, maximizing, minimizing and
// cycling focus. Binds are written as "MOD+KEY=action", for example
// "SUPER+C=colibry:close" or "ALT+F1=exec:foot".

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <xkbcommon/xkbcommon.h>

namespace Colibry {

    // Modifier bits used by bind descriptions.
    enum eModMask {
        BIND_SUPER = 1,
        BIND_CTRL  = 2,
        BIND_ALT   = 4,
        BIND_SHIFT = 8,
    };

    class CKeybindManager {
      public:
        static CKeybindManager& get();

        // Registers one bind string; duplicates replace earlier ones.
        void addBind(const std::string& definition);

        // Runs the action bound to the given key press. Returns true when a
        // bind consumed the event.
        bool handle(xkb_keysym_t sym, uint32_t mods, bool pressed);

      private:
        CKeybindManager();
        ~CKeybindManager() = default;

        struct SBind {
            uint32_t      mods;
            xkb_keysym_t  sym;
            std::string   action;
        };

        std::vector<SBind> m_binds;

        // Modifier mask currently derived from live key events.
        uint32_t m_mods = 0;
    };

}
