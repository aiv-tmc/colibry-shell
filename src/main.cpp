// Entry point of the Colibry compositor.
//
// Safety design: a hard watchdog timer is armed before any initialization
// and disarms only when the first frame reaches the screen. A hang anywhere
// in startup therefore terminates the process by itself, and the seat
// daemon restores the text console on client disconnect. Crash signals are
// handled the same way.

#include "core/Compositor.hpp"

#include <csignal>
#include <cstdio>
#include <unistd.h>

// Watchdog timeout in seconds: startup must present a frame within this
// window or the process exits.
static constexpr unsigned int STARTUP_WATCHDOG_SECONDS = 30;

// Watchdog handler: async-signal-safe exit so the seat daemon restores the
// console even when the main loop never started.
static void watchdogHandler(int /*sig*/) {
    static const char MSG[] = "[colibry-shell] startup watchdog fired, exiting\n";
    write(STDERR_FILENO, MSG, sizeof(MSG) - 1);
    _exit(1);
}

// Crash handler: dying is safe here because the seat daemon restores the
// console when the client connection drops.
static void crashHandler(int sig) {
    const char* msg = sig == SIGSEGV ? "[colibry-shell] segfault, exiting\n" : "[colibry-shell] fatal signal, exiting\n";
    write(STDERR_FILENO, msg, 30);
    _exit(1);
}

int main() {
    // Blocks termination signals during early startup; handlers are
    // installed before the watchdog so no signal arrives unhandled.
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigprocmask(SIG_BLOCK, &blocked, nullptr);

    // Arms the startup watchdog before touching DRM or Wayland: any hang in
    // initialization self-terminates within the window.
    std::signal(SIGALRM, watchdogHandler);
    alarm(STARTUP_WATCHDOG_SECONDS);

    // Converts crashes into clean process exits with console restoration.
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGBUS, crashHandler);

    auto& compositor = ColibryShell::CCompositor::get();

    if (!compositor.init()) {
        compositor.shutdown();
        return 1;
    }

    // Startup succeeded far enough that the event loop can take over.
    const int status = compositor.run();

    compositor.shutdown();
    return status;
}
