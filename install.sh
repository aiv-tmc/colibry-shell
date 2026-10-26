#!/usr/bin/env bash
# Generic Linux installer for the ColibryShell compositor.
#
# Builds the project with CMake, installs the binaries, the Wayland session
# file, the wallpaper assets from assets/ and the default system
# configuration to /etc/colibry/colibry.toml (the configuration namespace
# belongs to the Colibry OS and stays unchanged).
#
# Usage: ./install.sh [--prefix /usr/local]
set -euo pipefail

PREFIX="/usr/local"
BUILD_DIR="build"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | head -n 10; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

for tool in cmake pkg-config; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: '$tool' is required" >&2; exit 1; }
done

for dep in aquamarine hyprutils wayland-server wayland-client libdrm gbm pixman-1 xkbcommon libpng libjpeg xcursor freetype2; do
    pkg-config --exists "$dep" || { echo "error: missing pkg-config dependency '$dep'" >&2; exit 1; }
done

rm -rf "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Binaries, session file and wallpapers; the system config lands in /etc
# only for system prefixes.
if [[ "$PREFIX" == "/usr" || "$PREFIX" == "/usr/local" ]]; then
    sudo cmake --install "$BUILD_DIR"

    if [[ -f assets/colibry.toml ]]; then
        sudo install -D -m 644 /dev/null /etc/colibry/colibry.toml
        # Installs the shipped config, expanding the prefix placeholder and
        # keeping a backup of a previously edited system config.
        sed "s|@PREFIX@|$PREFIX|g" assets/colibry.toml | sudo tee /etc/colibry/colibry.toml.colibry-new >/dev/null
        if [[ -f /etc/colibry/colibry.toml ]]; then
            sudo cp /etc/colibry/colibry.toml /etc/colibry/colibry.toml.backup 2>/dev/null || true
        fi
        sudo mv /etc/colibry/colibry.toml.colibry-new /etc/colibry/colibry.toml
        echo "installed /etc/colibry/colibry.toml from assets/colibry.toml (previous config backed up to colibry.toml.backup)"
    fi
else
    cmake --install "$BUILD_DIR"
fi

echo
echo "ColibryShell installed to $PREFIX"
echo "Config: /etc/colibry/colibry.toml"
echo "Ctrl+Alt+Del exits the session."
