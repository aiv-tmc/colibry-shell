// Implementation of the control socket.

#include "IPC.hpp"

#include "../core/Compositor.hpp"
#include "../core/Output.hpp"
#include "../protocol/Display.hpp"
#include "../wallpaper/WallpaperEngine.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace ColibryShell {

    static constexpr const char* SOCKET_NAME = "colibry-shell.sock";

    CIPC::~CIPC() {
        if (m_socketFD >= 0)
            close(m_socketFD);
    }

    bool CIPC::start(CEventLoop& loop) {
        const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
        if (!runtimeDir || !*runtimeDir) {
            std::fprintf(stderr, "[colibry-shell] IPC: XDG_RUNTIME_DIR is not set\n");
            return false;
        }

        m_socketFD = socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_socketFD < 0) {
            std::fprintf(stderr, "[colibry-shell] IPC: socket failed: %s\n", std::strerror(errno));
            return false;
        }

        const std::string path = std::string(runtimeDir) + "/" + SOCKET_NAME;

        std::error_code ec;
        std::filesystem::remove(path, ec);

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (bind(m_socketFD, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::fprintf(stderr, "[colibry-shell] IPC: bind failed: %s\n", std::strerror(errno));
            close(m_socketFD);
            m_socketFD = -1;
            return false;
        }

        if (listen(m_socketFD, 4) != 0) {
            std::fprintf(stderr, "[colibry-shell] IPC: listen failed: %s\n", std::strerror(errno));
            close(m_socketFD);
            m_socketFD = -1;
            return false;
        }

        chmod(path.c_str(), 0600);

        loop.addFD(m_socketFD, POLLIN, [this](short) { onAccept(); });

        std::fprintf(stderr, "[colibry-shell] IPC socket: %s\n", path.c_str());
        return true;
    }

    void CIPC::onAccept() {
        const auto client = accept(m_socketFD, nullptr, nullptr);
        if (client < 0)
            return;

        char buffer[1024] = {0};
        const auto count  = read(client, buffer, sizeof(buffer) - 1);
        if (count <= 0) {
            close(client);
            return;
        }

        std::string request(buffer, (size_t)count);
        while (!request.empty() && (request.back() == '\n' || request.back() == '\r'))
            request.pop_back();

        const auto reply = answer(request);

        write(client, reply.data(), reply.size());
        close(client);
    }

    std::string CIPC::answer(const std::string& request) {
        auto& compositor = CCompositor::get();

        // "version" reports the compositor build version.
        if (request == "version")
            return std::string(COLIBRYSHELL_VERSION) + "\n";

        // "exit" asks the compositor to leave the event loop.
        if (request == "exit") {
            CCompositor::requestExit();
            return "ok\n";
        }

        // "wallpaper <path>" swaps the wallpaper at runtime.
        if (request.rfind("wallpaper ", 0) == 0) {
            const auto path = request.substr(10);

            if (!compositor.m_wallpaper)
                return "wallpaper engine unavailable\n";

            compositor.m_wallpaper->load(path);

            // Force a wallpaper re-rasterization and a full redraw on every
            // output.
            for (const auto& output : compositor.m_outputs)
                output->reloadBackground();

            return "ok\n";
        }

        return "unknown command\n";
    }

}
