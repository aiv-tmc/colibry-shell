# Build definition for the Colibry compositor.
#
# Stage 0 compiles: the startup skeleton with backend initialization through
# Aquamarine, per-output solid-color presentation, a poll-based event loop,
# configuration parsing, and the colibryctl IPC client.
project(
  'colibry',
  'cpp',
  version: '0.1.0',
  license: 'BSD-3-Clause',
  default_options: [
    'cpp_std=c++23',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

# Dependencies resolved through pkg-config.
dep_aquamarine = dependency('aquamarine')
dep_hyprutils = dependency('hyprutils')
dep_hyprlang = dependency('hyprlang')
dep_hyprgraphics = dependency('hyprgraphics')
dep_wayland_server = dependency('wayland-server')
dep_libdrm = dependency('libdrm')
dep_gbm = dependency('gbm')
dep_glesv2 = dependency('glesv2')
dep_egl = dependency('egl')

compositor_deps = [
  dep_aquamarine,
  dep_hyprutils,
  dep_hyprlang,
  dep_hyprgraphics,
  dep_wayland_server,
  dep_libdrm,
  dep_gbm,
  dep_glesv2,
  dep_egl,
]

compositor_sources = files(
  'src/main.cpp',
  'src/core/EventLoop.cpp',
  'src/core/Compositor.cpp',
  'src/core/Output.cpp',
  'src/config/ConfigManager.cpp',
  'src/ipc/IPC.cpp',
)

# The compositor binary installed to bindir.
executable(
  'colibry',
  compositor_sources,
  dependencies: compositor_deps,
  install: true,
)

# The IPC client binary installed to bindir.
executable(
  'colibryctl',
  files('src/ipc/ctl.cpp'),
  dependencies: dep_hyprutils,
  install: true,
)
