#!/usr/bin/env python3
# IPC fuzz harness for the Colibry compositor (stage 8).
#
# Floods the colibry IPC socket with random and adversarial payloads and
# verifies the compositor process stays alive. Run while the compositor is
# running:
#
#     ./build/colibry & sleep 2
#     python3 tests/fuzz_ipc.py $(pgrep -n colibry)
#     ./build/colibryctl exit

import os
import random
import socket
import string
import sys
import time

# Number of fuzz iterations against a live compositor.
ITERATIONS = 500

# Payload fragments that mimic and extend real commands.
FRAGMENTS = [
    b"version",
    b"exit",
    b"wallpaper ",
    b"wallpaper " + b"/" + b"a" * 300,
    b"\x00\x01\x02\xff\xfe",
    b"wallpaper\x00/etc/passwd",
    b" ",
    b"",
]


def socket_path() -> str:
    runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    return os.path.join(runtime, "colibry.sock")


def alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def random_payload() -> bytes:
    kind = random.randrange(3)
    if kind == 0:
        # Random printable text.
        length = random.randrange(0, 512)
        return "".join(random.choices(string.printable, k=length)).encode()
    if kind == 1:
        # Random binary.
        length = random.randrange(1, 256)
        return bytes(random.randrange(256) for _ in range(length))
    # Built from command fragments.
    return b"".join(random.choice(FRAGMENTS) for _ in range(random.randrange(1, 5)))


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <compositor-pid>")
        return 2

    pid = int(sys.argv[1])
    path = socket_path()

    if not os.path.exists(path):
        print(f"fuzz: socket {path} does not exist")
        return 2

    random.seed()

    for i in range(ITERATIONS):
        try:
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.settimeout(1.0)
            client.connect(path)
            client.sendall(random_payload())
            try:
                client.recv(4096)
            except socket.timeout:
                pass
            client.close()
        except OSError:
            # A dead or refusing socket is reported only if the process
            # died as well.
            pass

        if not alive(pid):
            print(f"fuzz: compositor died at iteration {i}")
            return 1

        if i % 100 == 0:
            time.sleep(0.05)

    print(f"fuzz: PASS ({ITERATIONS} iterations, compositor alive)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
