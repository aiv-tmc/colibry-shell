// Poll-based event loop with timers for the Colibry compositor.

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace ColibryShell {

    struct SLoopFD {
        int fd;
        short events;
        std::function<void(short revents)> callback;
    };

    struct SLoopTimer {
        uint64_t              periodMs;
        uint64_t              nextMs;
        std::function<void()> callback;
    };

    class CEventLoop {
      public:
        CEventLoop()          = default;
        ~CEventLoop()         = default;

        CEventLoop(const CEventLoop&)            = delete;
        CEventLoop& operator=(const CEventLoop&) = delete;

        // Registers a file descriptor; an already-registered descriptor has
        // its callback replaced, so backends can re-announce their fds
        // (for example after a VT switch) without duplicates.
        void addFD(int fd, short events, std::function<void(short)> callback);

        void addTimer(uint64_t periodMs, std::function<void()> callback);

        int  run();
        void quit();

      private:
        void dispatchTimers();

        std::vector<SLoopFD>    m_fds;
        std::vector<SLoopTimer> m_timers;

        bool m_running = false;
    };

}
