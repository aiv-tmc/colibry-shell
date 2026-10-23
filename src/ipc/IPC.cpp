// Implementation of the IPC server.

#include "IPC.hpp"

#include "../config/ConfigManager.hpp"
#include "../core/Compositor.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <poll.h>

namespace Colibry {

    // Maximum accepted request length in bytes.
    static constexpr size_t MAX_REQUEST = 4096;

    // Builds the absolute socket path inside the runtime directory.
    static std::string socketPath() {
        const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
        if (!runtimeDir || !*runtimeDir)
            runtimeDir = "/tmp";

        std::string path = std::string(runtimeDir) + "/colibry.sock";

        if (path.size() >= sizeof(sockaddr_un::sun_path)) {
            std::fprintf(stderr, "[colibry] socket path too long: %s\n", path.c_str());
            return "";
        }
        return path;
    }

    bool CIPC::start(CEventLoop& loop) {
        const auto path = socketPath();
        if (path.empty())
            return false;

        m_socketFD = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_socketFD < 0) {
            std::fprintf(stderr, "[colibry] failed to create IPC socket: %s\n", std::strerror(errno));
            return false;
        }

        sockaddr_un addr = {};
        addr.sun_family  = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        unlink(path.c_str());

        if (bind(m_socketFD, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::fprintf(stderr, "[colibry] failed to bind IPC socket: %s\n", std::strerror(errno));
            close(m_socketFD);
            m_socketFD = -1;
            return false;
        }

        if (listen(m_socketFD, 8) < 0) {
            std::fprintf(stderr, "[colibry] failed to listen on IPC socket: %s\n", std::strerror(errno));
            close(m_socketFD);
            m_socketFD = -1;
            return false;
        }

        loop.addFD(m_socketFD, POLLIN, [this](short) { acceptClient(); });

        std::fprintf(stderr, "[colibry] IPC listening on %s\n", path.c_str());
        return true;
    }

    void CIPC::stop() {
        if (m_socketFD >= 0) {
            const auto path = socketPath();
            if (!path.empty())
                unlink(path.c_str());
            close(m_socketFD);
            m_socketFD = -1;
        }
    }

    void CIPC::acceptClient() {
        const int fd = accept4(m_socketFD, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0)
            return;

        serveClient(fd);
        close(fd);
    }

    void CIPC::serveClient(int fd) {
        char buffer[MAX_REQUEST + 1] = {0};

        const ssize_t received = recv(fd, buffer, MAX_REQUEST, 0);
        if (received <= 0)
            return;

        std::string request(buffer, static_cast<size_t>(received));

        while (!request.empty() && (request.back() == '\n' || request.back() == '\r' || request.back() == ' '))
            request.pop_back();

        const std::string reply = answer(request);

        send(fd, reply.c_str(), reply.size(), MSG_NOSIGNAL);
    }

    std::string CIPC::answer(const std::string& request) {
        if (request == "version")
            return std::string("colibry ") + COLIBRY_VERSION + "\n";

        // "exit" asks the compositor to shut down cleanly.
        if (request == "exit") {
            CCompositor::requestExit();
            return "bye\n";
        }

        // "wallpaper <path>" reloads the wallpaper image at runtime.
        if (request.rfind("wallpaper ", 0) == 0) {
            const auto path = request.substr(10);

            auto& compositor = CCompositor::get();
            if (!compositor.m_wallpaper)
                return "wallpaper engine unavailable\n";

            compositor.m_wallpaper->load(path);
            compositor.addFullDamage();
            return "ok\n";
        }

        return "unknown command\n";
    }

}
