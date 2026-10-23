# Package derivation for the Colibry compositor.
#
# Builds the CMake project and ships the wallpaper assets and the default
# configuration template from assets/ (the template keeps the @PREFIX@
# placeholder; the NixOS module generates the system configuration itself).
{ lib
, stdenv
, cmake
, ninja
, pkg-config
, wayland-scanner
, aquamarine
, hyprutils
, wayland
, wayland-protocols
, libdrm
, mesa
, pixman
, libxkbcommon
, libpng
, libjpeg-turbo
, libXcursor
, freetype
, udev
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "colibry";
  version = "0.1.0";

  src = lib.cleanSource ../.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wayland-scanner
  ];

  buildInputs = [
    aquamarine
    hyprutils
    wayland
    wayland-protocols
    libdrm
    mesa
    pixman
    libxkbcommon
    libpng
    libjpeg-turbo
    libXcursor
    freetype
    udev
  ];

  # Ships the wallpaper assets, the configuration template and the session
  # desktop file.
  postInstall = ''
    mkdir -p $out/share/colibry/wallpapers
    if [ -d $src/assets ]; then
      cp -r $src/assets/* $out/share/colibry/wallpapers/ 2>/dev/null || true
    fi

    if [ -f $src/assets/colibry.toml ]; then
      mkdir -p $out/share/colibry
      cp $src/assets/colibry.toml $out/share/colibry/colibry.toml
    fi

    mkdir -p $out/share/wayland-sessions
    cat > $out/share/wayland-sessions/colibry.desktop <<EOF
    [Desktop Entry]
    Name=Colibry
    Comment=Colibry Wayland compositor
    Exec=$out/bin/colibry
    Type=Application
    EOF
  '';

  meta = with lib; {
    description = "Colibry Wayland compositor built on the Aquamarine backend";
    license = licenses.bsd3;
    platforms = platforms.linux;
    mainProgram = "colibry";
  };
})
