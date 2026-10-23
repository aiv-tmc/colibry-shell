{
  # Top-level flake of the Colibry compositor project.
  #
  # This flake exposes:
  #   - overlays.default : adds the "colibry" package to nixpkgs
  #   - packages.<system>.default : the colibry compositor package
  #   - devShells.<system>.default : a shell with all build dependencies
  #   - nixosModules.default : the NixOS module enabling a Colibry session
  description = "Colibry - a from-scratch Wayland compositor on the Aquamarine backend library";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      # Architectures that Colibry is built and packaged for.
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];

      # Helper that instantiates nixpkgs once per supported system.
      forEachSystem = f:
        nixpkgs.lib.genAttrs supportedSystems (system:
          f (import nixpkgs { inherit system; }));
    in
    {
      # Overlay injecting the colibry package into an existing nixpkgs set.
      overlays.default = final: _prev: {
        colibry = final.callPackage ./nixos/package.nix { };
      };

      # Per-system package output: the compositor built with the overlay applied.
      packages = forEachSystem (pkgs: {
        default = (pkgs.extend self.overlays.default).colibry;
      });

      # Per-system development shell containing the full native build toolchain.
      devShells = forEachSystem (pkgs: {
        default = pkgs.mkShell {
          # Compiler, build system and protocol scanners.
          nativeBuildInputs = with pkgs; [
            clang
            meson
            ninja
            pkg-config
            wayland-scanner
            hyprwayland-scanner
          ];

          # Libraries the compositor links against.
          buildInputs = with pkgs; [
            aquamarine      # DRM/KMS + libinput backend engine
            hyprutils       # math, signals, os utilities
            hyprlang        # configuration parser
            hyprgraphics    # colors, EGL helpers, textures
            hyprwayland-scanner # protocol C++ wrapper generator
            wayland         # core Wayland libraries
            wayland-protocols # standard Wayland protocol XML files
            libinput        # input device handling (pulled in by aquamarine)
            libdrm          # DRM definitions and format constants
            mesa            # GBM + EGL + GLESv2
            libxkbcommon    # keyboard layout handling
            libdisplay-info # EDID parsing for output modes
            udev            # device enumeration
          ];
        };
      });

      # NixOS module consumed via "services.colibry".
      nixosModules.default = ./nixos/module.nix;
    };
}
