// TOML configuration for the Colibry compositor.
//
// The manager loads the system config and merges the user override on
// top. Unknown keys are ignored so newer configs stay loadable.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ColibryShell {

    struct SMonitorRule {
        std::string name;
        std::string resolution;
        double      scale = 1.0;
    };

    class CConfigManager {
      public:
        static CConfigManager& get();

        // Loads the system config and merges the user override. Returns
        // false when neither exists; defaults remain in effect.
        bool load();

        const std::string& wallpaperPath() const;
        const std::string& wallpaperFit() const;

        const std::string& cursorTheme() const;
        int                cursorSize() const;
        double             cursorSpeed() const;

        bool followMouse() const;

        bool   shadowEnabled() const;
        int    shadowOffsetX() const;
        int    shadowOffsetY() const;
        int    shadowRadius() const;
        int    shadowRounding() const;
        double shadowOpacity() const;

        int titlebarHeight() const;
        int titlebarButtonWidth() const;
        const std::string& titlebarFont() const;

        int      windowRounding() const;
        int      screenRounding() const;
        uint64_t openAnimMs() const;
        int      loginFadeMs() const;
        int      loginUnlockMs() const;

        const std::string& terminal() const;

        const std::vector<SMonitorRule>& monitorRules() const;
        const std::vector<std::string>&  binds() const;

      private:
        bool loadFile(const std::string& path);
        void applyKey(const std::string& section, const std::string& key, const std::string& value);

        std::string m_wallpaperPath;
        std::string m_wallpaperFit = "cover";
        std::string m_cursorTheme  = "Adwaita";
        int         m_cursorSize   = 24;
        double      m_cursorSpeed  = 1.0;
        bool        m_followMouse  = true;

        bool   m_shadowEnabled  = true;
        int    m_shadowOffsetX  = 0;
        int    m_shadowOffsetY  = 12;
        int    m_shadowRadius   = 24;
        int    m_shadowRounding = 8;
        double m_shadowOpacity  = 0.45;

        int         m_titlebarHeight      = 32;
        int         m_titlebarButtonWidth = 46;
        std::string m_titlebarFont;

        int      m_windowRounding = 10;
        int      m_screenRounding = 0;
        uint64_t m_openAnimMs     = 220;
        int      m_loginFadeMs    = 750;
        int      m_loginUnlockMs  = 250;

        std::string m_terminal = "ghostty";

        std::vector<SMonitorRule> m_monitorRules;
        std::vector<std::string>  m_binds;

      public:
        // Public: the compositor holds the manager through a shared
        // pointer built with makeShared.
        CConfigManager()  = default;
        ~CConfigManager() = default;
    };

}
