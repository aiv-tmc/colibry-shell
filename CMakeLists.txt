# Build definition for the ColibryShell compositor.

cmake_minimum_required(VERSION 3.27)

project(
  colibryshell
  VERSION 0.2.0
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
pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(LIBPNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)
pkg_check_modules(XCURSOR REQUIRED IMPORTED_TARGET xcursor)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)

add_compile_definitions(COLIBRYSHELL_VERSION="${PROJECT_VERSION}")

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

pkg_get_variable(WAYLAND_SERVER_PKGDATADIR wayland-server pkgdatadir)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

set(PROTOCOL_XMLS
  "${WAYLAND_SERVER_PKGDATADIR}/wayland.xml"
  "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml"
  "${WAYLAND_PROTOCOLS_DIR}/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml"
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

add_library(colibryshell-protocols STATIC "${PROTOCOL_GENERATED}")
target_include_directories(colibryshell-protocols PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(
  colibry-shell
  src/main.cpp
  src/core/EventLoop.cpp
  src/core/Compositor.cpp
  src/core/Output.cpp
  src/config/ConfigManager.cpp
  src/ipc/IPC.cpp
  src/protocol/Display.cpp
  src/protocol/Buffer.cpp
  src/protocol/Region.cpp
  src/protocol/Subcompositor.cpp
  src/protocol/Surface.cpp
  src/protocol/Seat.cpp
  src/protocol/XdgShell.cpp
  src/protocol/DataDevice.cpp
  src/protocol/DMABuf.cpp
  src/protocol/OutputInfo.cpp
  src/protocol/ForeignToplevel.cpp
  src/desktop/Window.cpp
  src/desktop/Cloth.cpp
  src/desktop/Cursor.cpp
  src/desktop/Keybinds.cpp
  src/desktop/DragManager.cpp
  src/desktop/Switcher.cpp
  src/desktop/Launcher.cpp
  src/render/GLEngine.cpp
  src/render/GLCompositor.cpp
  src/render/SceneDraw.cpp
  src/render/SwitcherDraw.cpp
  src/render/LauncherDraw.cpp
  src/render/TitleText.cpp
  src/render/SolidRenderer.cpp
  src/wallpaper/WallpaperEngine.cpp
)

target_include_directories(colibry-shell PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(
  colibry-shell
  PRIVATE
  colibryshell-protocols
  PkgConfig::AQUAMARINE
  PkgConfig::HYPRUTILS
  PkgConfig::WAYLAND_SERVER
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::LIBDRM
  PkgConfig::GBM
  PkgConfig::GLESV2
  PkgConfig::EGL
  PkgConfig::PIXMAN
  PkgConfig::XKBCOMMON
  PkgConfig::LIBPNG
  PkgConfig::LIBJPEG
  PkgConfig::XCURSOR
  PkgConfig::FREETYPE
)

add_executable(colibryctl src/ipc/ctl.cpp)

install(TARGETS colibry-shell colibryctl RUNTIME DESTINATION bin)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION share/colibry-shell/wallpapers
        FILES_MATCHING PATTERN "*.png" PATTERN "*.jpg" PATTERN "*.jpeg")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/colibry.toml"
        DESTINATION share/colibry
        OPTIONAL)

set(COLIBRYSHELL_SESSION_DIR "share/wayland-sessions")
install(CODE "
  file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${COLIBRYSHELL_SESSION_DIR}\")
  file(WRITE \"\${CMAKE_INSTALL_PREFIX}/${COLIBRYSHELL_SESSION_DIR}/colibry-shell.desktop\"
\"[Desktop Entry]
Name=Colibry Shell
Comment=Colibry Shell Wayland compositor
Exec=\${CMAKE_INSTALL_PREFIX}/bin/colibry-shell
Type=Application
\")
")

# Unit tests run head-less (no GPU, no Wayland session). Each test file is
# its own executable because every test carries its own main().
include(CTest)
if(BUILD_TESTING)
  add_executable(colibry-test-cloth
    tests/test_cloth.cpp
    src/desktop/Cloth.cpp
  )
  target_link_libraries(colibry-test-cloth PRIVATE PkgConfig::HYPRUTILS)

  add_executable(colibry-test-chromegeom
    tests/test_chromegeom.cpp
    src/config/ConfigManager.cpp
  )
  target_link_libraries(colibry-test-chromegeom PRIVATE PkgConfig::HYPRUTILS)

  add_test(NAME cloth COMMAND colibry-test-cloth)
  set_tests_properties(cloth PROPERTIES PASS_REGULAR_EXPRESSION "all tests passed")

  add_test(NAME chromegeom COMMAND colibry-test-chromegeom)
  set_tests_properties(chromegeom PROPERTIES PASS_REGULAR_EXPRESSION "all tests passed")
endif()
