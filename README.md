# Colibry

A from-scratch Wayland compositor written in C++23 on top of the Aquamarine backend library (KMS/DRM, libinput, session management). No code from Hyprland, wlroots, KDE, or GNOME — only Aquamarine, Hyprutils, and a hand-written protocol layer.

The shell does everything in one process: GPU compositing, server-side window decorations, elastic window physics, wallpaper, clipboard, window switcher, and an application launcher.

## Highlights

- **GPU-only compositing (GLES2/EGL).** Damage-driven frames: a still desktop produces zero GPU and zero CPU work. One fullscreen window without overlays takes the direct fast path — a single textured quad per damaged rectangle.
- **Elastic cloth windows.** Every window box is a verlet-simulated mesh. Dragging stretches the surface from the grab point while the rest follows; tiling, snapping, maximizing, and workspace changes make the material visibly settle. Opens grow in with an overshoot, closes shrink out with a fade.
- **Buttery pointer.** Exponentially smoothed motion with velocity extrapolation, hard walls at screen edges and rounded corners, idle shrink-and-fade with a symmetric reappear animation.
- **Adaptive titlebars.** A gradient derived from the window's own top pixels (via a GLES shader), FreeType titles, background-less min/max/close glyphs with hover state, drag anywhere, double-click maximize.
- **GPU shadows.** Blurred silhouettes generated on the GPU, cached per size, offset/opacity configurable.
- **macOS-style launcher.** SUPER+D / SUPER+Space: fuzzy search over desktop entries, grid of translucent tiles.
- **Cinematic switcher.** SUPER+Tab: the desktop blurs, the unselected area dims in a gradient around the selected card, cards show live window content with white borders.
- **Rounded everything, for real.** Signed-distance corner shaders for windows and the screen itself; screen corners are pure black and untouchable — the cursor slides along the arcs.

## Status

Working:

- Floating windows by default; explicit master/stack tiling; workspaces 1–10 with move/send keybinds.
- Fullscreen-at-open windows (games, viewers) land on a fresh empty workspace.
- Server-side decorations with hover/press buttons and transparent-pixel content drags.
- Edge and corner resizing; snap zones (half/quarter/fullscreen) with elastic settle.
- Clipboard between clients; `foreign-toplevel` and `wl_output` for panels.
- Keyboard and mouse parity (`docs/input-parity.md` if present).
- IPC: `colibryctl version | exit | wallpaper <path>`.

Limitations:

- Nested (Wayland/X11 backend) sessions are not supported — GPU rendering requires the DRM node.
- Single-plane dmabuf import only (plane 0, ARGB8888/XRGB8888); clients on shm work everywhere.
- No layer-shell, XWayland, drag-and-drop, touchpad gestures, or popup rendering yet.
- Single output is the tested configuration; multi-output is cosmetic.

## Dependencies

Arch: `aquamarine hyprutils wayland wayland-protocols libdrm mesa pixman xkbcommon libpng libjpeg-turbo libxcursor freetype2`

A terminal for autostart: `foot` or `ghostty` (or set `terminal=` / `COLIBRY_TERMINAL`).

## Build & install

```sh
./build.sh --clean          # debug build into ./build
./build.sh --clean --asan   # with sanitizers

sudo ./install.sh                  # /usr/local, config from assets/
sudo ./install.sh --prefix /usr
```

NixOS: `services.colibry.enable = true;` (module in `nixos/module.nix`).

## Run

From a TTY — the autostart terminal opens by itself:

```sh
colibry
```

Exit: Ctrl+Alt+Del or `colibryctl exit`. Switch VTs: Ctrl+Alt+F1..F12. If no frame reaches the screen within 20 s, a watchdog terminates the process and returns the console.

## Configuration

`/etc/colibry/colibry.toml`, merged with `~/.config/colibry/colibry.toml`:

```toml
wallpaper = "/usr/share/colibry/wallpapers/default.jpg"
wallpaper_fit = "cover"          # or "fit"
cursor_theme = "Adwaita"
cursor_size = 24
terminal = "ghostty"
monitors = ["eDP-1,1920x1200,1", ",preferred,1.5"]
bind = ["SUPER+B=exec:firefox"]

window_rounding = 10
screen_rounding = 0
open_anim_ms = 220
shadow_enabled = 1
shadow_offset_y = 12
shadow_opacity = 0.45
titlebar_height = 32
```

Cloth feel is tuned in source: `CLOTH_DAMPING` / `CLOTH_PULL` in `src/desktop/Cloth.cpp`, cursor extrapolation in `src/desktop/Cursor.hpp`.

## Default keybinds

```
SUPER+Return, Alt+F1   terminal
SUPER+D, SUPER+Space   launcher
SUPER+Tab              window switcher (arrows/Tab cycle, Enter, Esc)
SUPER+J / SUPER+K      focus next / previous window
SUPER+C, SUPER+Q, Alt+F4   close (animated)
SUPER+F                fullscreen          SUPER+M  minimize
SUPER+Shift+T          float / tiling
SUPER+arrows-drag      move window (elastic cloth grab)
SUPER+1..0 / SUPER+Shift+1..0   workspaces / send window
Ctrl+Alt+Del           exit
```

Mouse: drag by the titlebar, resize at edges and corners, buttons in the titlebar, focus follows the pointer.

## Testing

```sh
tests/e2e/smoke.sh ./build/colibry      # from TTY
python3 tests/fuzz_ipc.py $(pgrep -n colibry)
./build/colibryctl exit
```

## Repository

```
src/core/       compositor coordinator, event loop, output presentation
src/protocol/   Wayland: display, shm, surface, xdg-shell, seat,
                clipboard, wl_output, linux-dmabuf, foreign-toplevel
src/desktop/    windows, cloth physics, chrome geometry, drag gestures,
                cursor, keybinds, switcher, launcher
src/render/     EGL/GLES engine, shaders, scene passes, text rasterization
src/wallpaper/  wallpaper scaling
src/config/     TOML configuration
src/ipc/        control socket and colibryctl
tests/          smoke and fuzz tests
protocols/      vendored protocol XML
assets/         wallpapers and config template
nixos/          package and module
```

Architecture and developer guide: see the project wiki (`Architecture` page).

## Roadmap

Layer-shell, full dmabuf multi-plane, XWayland, drag-and-drop, touchpad gestures, multi-output workspaces, popup rendering.

License: MPL v2.0.
