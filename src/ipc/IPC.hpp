// IPC server of the Colibry compositor.
//
// Listens on a unix socket at $XDG_RUNTIME_DIR/colibry.sock and answers
// line-based requests from the colibryctl client. At this stage the only
// supported request is "version".

#pragma once

#include "../core/EventLoop.hpp"

#include <string>

namespace Colibry {

    class CIPC {
      public:
        CIPC()          = default;
        ~CIPC()         = default;

        // Creates the socket, binds it and registers it with the event loop.
        // Returns false when the socket cannot be opened; this is not fatal.
        bool start(CEventLoop& loop);

        // Closes the listening socket and unlinks its path.
        void stop();

        // Builds the reply for a single request line.
        static std::string answer(const std::string& request);

      private:
        // Accepts one pending client connection.
        void acceptClient();

        // Reads one request and writes the reply on a connected client.
        void serveClient(int fd);

        int m_socketFD = -1;
    };

}
