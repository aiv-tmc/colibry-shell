// colibryctl - command line client for the Colibry IPC socket.
//
// Joins its arguments into a single request line, sends it to the compositor
// socket and prints the reply to standard output.

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Builds the absolute socket path shared with the compositor.
static std::string socketPath() {
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir || !*runtimeDir)
        runtimeDir = "/tmp";

    return std::string(runtimeDir) + "/colibry.sock";
}

int main(int argc, char** argv) {
    // Joins all arguments with single spaces into one request line.
    std::string request;
    for (int i = 1; i < argc; ++i) {
        if (i > 1)
            request += ' ';
        request += argv[i];
    }

    if (request.empty()) {
        std::fprintf(stderr, "usage: colibryctl <command>\n");
        return 1;
    }

    const auto path = socketPath();
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        std::fprintf(stderr, "colibryctl: socket path too long\n");
        return 1;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        std::fprintf(stderr, "colibryctl: socket: %s\n", std::strerror(errno));
        return 1;
    }

    sockaddr_un addr = {};
    addr.sun_family  = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "colibryctl: connect: %s\n", std::strerror(errno));
        close(fd);
        return 1;
    }

    if (send(fd, request.c_str(), request.size(), MSG_NOSIGNAL) < 0) {
        std::fprintf(stderr, "colibryctl: send: %s\n", std::strerror(errno));
        close(fd);
        return 1;
    }

    // Streams the reply to stdout until the compositor closes the connection.
    char buffer[4096];
    ssize_t received;
    while ((received = recv(fd, buffer, sizeof(buffer), 0)) > 0)
        fwrite(buffer, 1, static_cast<size_t>(received), stdout);

    close(fd);
    return 0;
}
