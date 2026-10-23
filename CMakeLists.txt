# Build definition for the Colibry compositor.

cmake_minimum_required(VERSION 3.27)

project(
  colibry
  VERSION 0.1.0
  DESCRIPTION "A from-scratch Wayland compositor on the Aquamarine backend library"
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(AQUAMARINE REQUIRED IMPORTED_TARGET aquamarine)
pkg_check_modules(HYPRUTILS REQUIRED IMPORTED_TARGET hyprutils)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(LIBPNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)
pkg_check_modules(XCURSOR REQUIRED IMPORTED_TARGET xcursor)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)

add_compile_definitions(COLIBRY_VERSION="${PROJECT_VERSION}")

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

pkg_get_variable(WAYLAND_SERVER_PKGDATADIR wayland-server pkgdatadir)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

set(PROTOCOL_XMLS
  "${WAYLAND_SERVER_PKGDATADIR}/wayland.xml"
  "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml"
  "${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml"
)

set(PROTOCOL_GENERATED)

foreach(xml ${PROTOCOL_XMLS})
  get_filename_component(protocol_name "${xml}" NAME_WE)
  set(code "${CMAKE_CURRENT_BINARY_DIR}/${protocol_name}-protocol.c")
  set(header "${CMAKE_CURRENT_BINARY_DIR}/${protocol_name}-server-protocol.h")

  add_custom_command(
    OUTPUT "${code}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${code}"
    DEPENDS "${xml}"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${header}"
    COMMAND "${WAYLAND_SCANNER}" server-header "${xml}" "${header}"
    DEPENDS "${xml}"
    VERBATIM
  )

  list(APPEND PROTOCOL_GENERATED "${code}" "${header}")
endforeach()

add_library(colibry-protocols STATIC "${PROTOCOL_GENERATED}")
target_include_directories(colibry-protocols PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(
  colibry
  src/main.cpp
  src/core/EventLoop.cpp
  src/core/Compositor.cpp
  src/core/Output.cpp
  src/config/ConfigManager.cpp
  src/ipc/IPC.cpp
  src/protocol/Display.cpp
  src/protocol/Buffer.cpp
  src/protocol/Region.cpp
  src/protocol/Surface.cpp
  src/protocol/Seat.cpp
  src/protocol/XdgShell.cpp
  src/protocol/DataDevice.cpp
  src/protocol/OutputInfo.cpp
  src/protocol/ForeignToplevel.cpp
  src/desktop/Window.cpp
  src/desktop/Cursor.cpp
  src/desktop/Keybinds.cpp
  src/desktop/DragManager.cpp
  src/desktop/TitleBar.cpp
  src/desktop/Shadow.cpp
  src/desktop/Layout.cpp
  src/desktop/Switcher.cpp
  src/render/SolidRenderer.cpp
  src/wallpaper/WallpaperEngine.cpp
)

target_include_directories(colibry PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(
  colibry
  PRIVATE
  colibry-protocols
  PkgConfig::AQUAMARINE
  PkgConfig::HYPRUTILS
  PkgConfig::WAYLAND_SERVER
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::LIBDRM
  PkgConfig::GBM
  PkgConfig::PIXMAN
  PkgConfig::XKBCOMMON
  PkgConfig::LIBPNG
  PkgConfig::LIBJPEG
  PkgConfig::XCURSOR
  PkgConfig::FREETYPE
)

add_executable(colibryctl src/ipc/ctl.cpp)

install(TARGETS colibry colibryctl RUNTIME DESTINATION bin)

# Installs the shipped wallpapers and the default configuration template
# with the prefix placeholder resolved. Other files from assets/ (the
# config) are handled by install(CODE) below.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION share/colibry/wallpapers
        FILES_MATCHING PATTERN "*.png" PATTERN "*.jpg" PATTERN "*.jpeg")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/colibry.toml"
        DESTINATION share/colibry
        OPTIONAL)

set(COLIBRY_SESSION_DIR "share/wayland-sessions")
install(CODE "
  file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${COLIBRY_SESSION_DIR}\")
  file(WRITE \"\${CMAKE_INSTALL_PREFIX}/${COLIBRY_SESSION_DIR}/colibry.desktop\"
\"[Desktop Entry]
Name=Colibry
Comment=Colibry Wayland compositor
Exec=\${CMAKE_INSTALL_PREFIX}/bin/colibry
Type=Application
\")
")

# Materializes /etc/colibry/colibry.toml from the shipped template when the
# user has none; @PREFIX@ expands to the installation prefix.
install(CODE "
  set(TEMPLATE \"\${CMAKE_INSTALL_PREFIX}/share/colibry/colibry.toml\")
  if(EXISTS /etc/colibry/colibry.toml)
    message(STATUS \"keeping existing /etc/colibry/colibry.toml\")
  elseif(EXISTS \"\${TEMPLATE}\")
    file(READ \"\${TEMPLATE}\" COLIBRY_CONFIG)
    string(REPLACE \"@PREFIX@\" \"\${CMAKE_INSTALL_PREFIX}\" COLIBRY_CONFIG \"\${COLIBRY_CONFIG}\")
    file(MAKE_DIRECTORY /etc/colibry)
    file(WRITE /etc/colibry/colibry.toml \"\${COLIBRY_CONFIG}\")
    message(STATUS \"installed /etc/colibry/colibry.toml from the assets template\")
  else()
    file(MAKE_DIRECTORY /etc/colibry)
    file(WRITE /etc/colibry/colibry.toml \"# Colibry system configuration\nwallpaper = \\\"\\\"\nwallpaper_fit = \\\"cover\\\"\ncursor_theme = \\\"\\\"\ncursor_size = 24\nterminal = \\\"\\\"\nfollow_mouse = 1\nshadow_enabled = 1\nshadow_offset_x = 0\nshadow_offset_y = 12\nshadow_radius = 24\nshadow_rounding = 8\nshadow_opacity = 0.45\ntitlebar_height = 32\ntitlebar_button_width = 46\ntitlebar_font = \\\"\\\"\nmonitors = []\nbind = []\n\")
    message(STATUS \"installed a minimal /etc/colibry/colibry.toml\")
  endif()
")
