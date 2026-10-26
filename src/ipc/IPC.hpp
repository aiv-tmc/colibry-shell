// Control socket for the Colibry compositor.
//
// CIPC listens on $XDG_RUNTIME_DIR/colibry-shell.sock and answers one-line
// requests from the colibryctl utility: version, exit and runtime
// wallpaper swaps.

#pragma once

#include "../core/EventLoop.hpp"

#include <string>

namespace ColibryShell {

    class CIPC {
      public:
        CIPC()          = default;
        ~CIPC();

        // Binds the socket and registers it with the event loop. Returns
        // false when the socket could not be created.
        bool start(CEventLoop& loop);

        // Handles one request line and produces the reply.
        static std::string answer(const std::string& request);

      private:
        void onAccept();

        int m_socketFD = -1;
    };

}
