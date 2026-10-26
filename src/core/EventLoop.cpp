// Implementation of the poll-based event loop with timers.

#include "EventLoop.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <poll.h>
#include <vector>

namespace ColibryShell {

    static uint64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void CEventLoop::addFD(int fd, short events, std::function<void(short)> callback) {
        for (auto& entry : m_fds) {
            if (entry.fd == fd) {
                entry.events  = events;
                entry.callback = std::move(callback);
                return;
            }
        }
        m_fds.push_back(SLoopFD{.fd = fd, .events = events, .callback = std::move(callback)});
    }

    void CEventLoop::addTimer(uint64_t periodMs, std::function<void()> callback) {
        m_timers.push_back(SLoopTimer{.periodMs = periodMs, .nextMs = nowMs() + periodMs, .callback = std::move(callback)});
    }

    void CEventLoop::dispatchTimers() {
        const auto now = nowMs();

        for (auto& timer : m_timers) {
            if (now < timer.nextMs)
                continue;

            timer.callback();
            timer.nextMs += timer.periodMs;
            if (timer.nextMs <= now)
                timer.nextMs = now + timer.periodMs;
        }
    }

    int CEventLoop::run() {
        m_running = true;

        std::vector<pollfd> pollfds(m_fds.size());

        while (m_running) {
            for (size_t i = 0; i < m_fds.size(); ++i) {
                pollfds[i].fd      = m_fds[i].fd;
                pollfds[i].events  = m_fds[i].events;
                pollfds[i].revents = 0;
            }

            int timeoutMs = 1000;
            if (!m_timers.empty()) {
                uint64_t nearest = UINT64_MAX;
                const auto now   = nowMs();
                for (const auto& timer : m_timers)
                    nearest = std::min(nearest, timer.nextMs > now ? timer.nextMs - now : (uint64_t)0);
                timeoutMs = (int)std::min<uint64_t>(nearest, 1000);
            }

            int ret = poll(pollfds.data(), pollfds.size(), timeoutMs);
            if (ret < 0) {
                if (errno == EINTR) {
                    dispatchTimers();
                    continue;
                }
                return -1;
            }

            for (size_t i = 0; i < m_fds.size() && ret > 0; ++i) {
                if (pollfds[i].revents == 0)
                    continue;
                --ret;
                m_fds[i].callback(pollfds[i].revents);
            }

            dispatchTimers();
        }

        return 0;
    }

    void CEventLoop::quit() {
        m_running = false;
    }

}
