{
  description = "Pico 2 W FreeRTOS Bluetooth Keyboard Firmware";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    freertos = {
      url = "git+https://github.com/raspberrypi/FreeRTOS-Kernel.git?submodules=1";
      flake = false;
    };

    zmk-nix = {
      url = "github:lilyinstarlight/zmk-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, flake-utils, freertos, zmk-nix }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        pico-sdk-full = pkgs.pico-sdk.override { withSubmodules = true; };

        fw = pkgs.stdenv.mkDerivation {
          pname = "pico2w-keyboard-fw";
          version = "0.1.0";

          src = ./src;

          # Tools required to run the build process
          nativeBuildInputs = with pkgs; [
            cmake
            gcc-arm-embedded
            ninja
            python3
            picotool
            git
          ];
          buildInputs = with pkgs; [
            pico-sdk-full
          ];

          PICO_SDK_PATH = "${pico-sdk-full}/lib/pico-sdk";
          FREERTOS_KERNEL_PATH = freertos;

          cmakeFlags = [
            "-DCMAKE_C_COMPILER=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-gcc"
            "-DCMAKE_CXX_COMPILER=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-g++"
            "-DFREERTOS_KERNEL_PATH=${freertos}"
            "-DPICO_BOARD=pico2_w"
            "-G" "Ninja"
            "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
          ];

          configurePhase = ''
            cmake $src \
              -B build \
              -DCMAKE_C_COMPILER=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-gcc \
              -DCMAKE_CXX_COMPILER=${pkgs.gcc-arm-embedded}/bin/arm-none-eabi-g++ \
              -DFREERTOS_KERNEL_PATH=${freertos} \
              -DPICO_BOARD=pico2_w \
              -G Ninja \
              -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
          '';

          buildPhase = ''
            cd build
            ninja -j$NIX_BUILD_CORES
          '';

          installPhase = ''
            mkdir -p $out
            # Copy all generated firmware binaries to the output directory
            cp *.uf2 *.elf *.hex *.bin $out/ || echo "No binaries found to copy."
          '';
        };
      in {
        packages.default = fw;

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            gcc-arm-embedded
            ninja
            python3
          ];
          
          buildInputs = with pkgs; [
            pico-sdk-full
          ];
          
          shellHook = ''
            export FREERTOS_KERNEL_PATH="${freertos}"
            # FIX: Explicitly set it for your local IDE environment too
            export PICO_SDK_PATH="${pico-sdk-full}/lib/pico-sdk"
            echo "Pico 2 W FreeRTOS Development Environment Loaded."
            echo "PICO_SDK_PATH explicitly set to: $PICO_SDK_PATH"
          '';
        };
      }
    );
}
