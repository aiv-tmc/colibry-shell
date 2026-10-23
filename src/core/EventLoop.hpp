// Poll-based event loop with timers for the Colibry compositor.
//
// The loop owns watched file descriptors and recurring timers, dispatching
// readiness notifications and timer callbacks. The compositor drives its
// frame pacing through a 16 ms timer instead of relying on backend frame
// events.

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace Colibry {

    // A single watched file descriptor entry.
    struct SLoopFD {
        int fd;       // file descriptor watched with poll()
        short events; // mask of POLLIN / POLLOUT interest flags
        // Invoked when the descriptor reports any requested event.
        std::function<void(short revents)> callback;
    };

    // A recurring timer entry.
    struct SLoopTimer {
        uint64_t              periodMs; // interval between firings
        uint64_t              nextMs;   // next firing time, milliseconds
        std::function<void()> callback;
    };

    class CEventLoop {
      public:
        CEventLoop()          = default;
        ~CEventLoop()         = default;

        CEventLoop(const CEventLoop&)            = delete;
        CEventLoop& operator=(const CEventLoop&) = delete;

        // Registers a file descriptor for polling.
        void addFD(int fd, short events, std::function<void(short)> callback);

        // Registers a recurring timer; the callback fires every periodMs
        // milliseconds while the loop runs.
        void addTimer(uint64_t periodMs, std::function<void()> callback);

        // Runs the loop until quit() is called.
        int run();

        // Requests the running loop to exit after the current cycle.
        void quit();

      private:
        // Fires every timer whose deadline passed and reschedules it.
        void dispatchTimers();

        std::vector<SLoopFD>    m_fds;
        std::vector<SLoopTimer> m_timers;

        bool m_running = false;
    };

}
