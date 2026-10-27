// TOML configuration for the ColibryShell compositor.
//
// Search order: /etc/colibry/colibry.toml, then
// ~/.config/colibry/colibry.toml, then ./colibry.toml and
// ../colibry.toml (running uninstalled from build/ or the repo root).
// Later files override earlier ones; unknown keys are ignored. The
// effective values are echoed to the log after loading.

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

        bool load();

        const std::string& wallpaperPath() const;
        const std::string& wallpaperFit() const;

        int                cursorSize() const;
        double             cursorSpeed() const;
        double             cursorAcceleration() const;
        const std::string& cursorColor() const;

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

        bool hdrEnabled() const;
        int  dmabufMode() const;

        const std::string& terminal() const;

        const std::vector<SMonitorRule>& monitorRules() const;
        const std::vector<std::string>&  binds() const;

      private:
        bool loadFile(const std::string& path);
        void applyKey(const std::string& section, const std::string& key, const std::string& value);

        std::string m_wallpaperPath;
        std::string m_wallpaperFit = "cover";
        int         m_cursorSize   = 24;
        double      m_cursorSpeed  = 1.0;
        double      m_cursorAcceleration = 0.4;
        std::string m_cursorColor  = "";   // "" = classic white arrow
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
        int      m_screenRounding = 12;
        uint64_t m_openAnimMs     = 220;
        int      m_loginFadeMs    = 2500;
        int      m_loginUnlockMs  = 250;

        bool m_hdrEnabled = true;
        int  m_dmabufMode = -1;

        std::string m_terminal = "ghostty";

        std::vector<SMonitorRule> m_monitorRules;
        std::vector<std::string>  m_binds;

      public:
        CConfigManager()  = default;
        ~CConfigManager() = default;
    };

}
