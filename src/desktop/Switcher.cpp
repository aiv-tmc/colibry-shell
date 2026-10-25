// Implementation of the window switcher model.

#include "Switcher.hpp"

#include "../core/Compositor.hpp"
#include "Window.hpp"

#include <algorithm>

namespace Colibry {

    CSwitcher& CSwitcher::get() {
        static CSwitcher s_instance;
        return s_instance;
    }

    bool CSwitcher::active() const {
        return m_active;
    }

    const std::vector<CWindow*>& CSwitcher::entries() const {
        return m_entries;
    }

    int CSwitcher::selected() const {
        return m_index;
    }

    void CSwitcher::open() {
        auto& compositor = CCompositor::get();

        m_entries.clear();
        for (const auto& window : compositor.m_windows) {
            if (!window->mapped() || window->workspace() != compositor.currentWorkspace())
                continue;
            m_entries.push_back(window.get());
        }

        if (m_entries.empty())
            return;

        m_index = 0;
        const auto it = std::find(m_entries.begin(), m_entries.end(), compositor.m_focusedWindow);
        if (it != m_entries.end())
            m_index = (int)(it - m_entries.begin());

        m_active = true;
        compositor.addFullDamage();
    }

    void CSwitcher::cycle(int direction) {
        if (!m_active || m_entries.empty())
            return;

        const int count = (int)m_entries.size();
        m_index         = ((m_index + direction) % count + count) % count;
        CCompositor::get().addFullDamage();
    }

    void CSwitcher::activate() {
        if (!m_active)
            return;

        if (m_index >= 0 && m_index < (int)m_entries.size()) {
            auto* window = m_entries[m_index];
            window->setMinimized(false);
            CCompositor::get().focusWindow(window);
        }

        cancel();
    }

    void CSwitcher::cancel() {
        if (!m_active)
            return;

        m_active = false;
        m_entries.clear();
        CCompositor::get().addFullDamage();
    }

}
