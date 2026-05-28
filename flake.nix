{
  description = "Pico 2 W FreeRTOS Bluetooth Keyboard Firmware";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    freertos = {
      url = "git+https://github.com/raspberrypi/FreeRTOS-Kernel.git?submodules=1";
      flake = false;
    };

    openocd-src = {
      url = "git+https://github.com/raspberrypi/openocd.git?submodules=1";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, freertos, openocd-src }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        pico-sdk-full = pkgs.pico-sdk.override { withSubmodules = true; };

        openocd-pico = pkgs.openocd.overrideAttrs (old: {
          pname = "openocd-pico";
          version = "rp2350-latest";
          src = openocd-src;

          nativeBuildInputs = (old.nativeBuildInputs or []) ++ [
            pkgs.autoreconfHook
          ];
        });

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

            cp compile_commands.json $out
          '';
        };
      in {
        packages.default = fw;

        apps.flash = {
          type = "app";
          program = let
            flashScript = pkgs.writeShellScriptBin "flash-pico2w" ''
              # 1. Walk up the directory tree to find the project root containing flake.nix
              PROJECT_ROOT="$PWD"
              while [ ! -f "$PROJECT_ROOT/flake.nix" ]; do
                if [ "$PROJECT_ROOT" = "/" ]; then
                  echo "Error: flake.nix not found in current or any parent directories!"
                  exit 1
                fi
                PROJECT_ROOT=$(dirname "$PROJECT_ROOT")
              done

              echo "Project root found at: $PROJECT_ROOT"
              
              # 2. Change to the project root
              cd "$PROJECT_ROOT"
              cd debug

              # Ensure the config file actually exists where we expect it
              if [ ! -f "openocd.cfg" ]; then
                echo "Error: openocd.cfg not found!"
                exit 1
              fi

              # 3. Explicitly build the firmware
              echo "Building firmware..."
              nix build .

              if [ $? -ne 0 ]; then
                echo "Error: nix build failed!"
                exit 1
              fi

              # 4. Find the .elf file in the newly generated ./result directory
              ELF_FILE=$(ls ./result/*.elf | head -n 1)

              if [ -z "$ELF_FILE" ]; then
                echo "Error: No .elf file found in ./result/!"
                exit 1
              fi

              # 5. Flash the firmware using custom OpenOCD, correct script path, and Core 1 sysresetreq fix
              echo "Flashing: $ELF_FILE"
              ${openocd-pico}/bin/openocd \
                -s ${openocd-pico}/share/openocd/scripts \
                -f openocd.cfg \
                -c "program \"$ELF_FILE\" verify reset exit"
            '';
          in "${flashScript}/bin/flash-pico2w";
        };

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            gcc-arm-embedded
            ninja
            python3
            picotool
            openocd-pico
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
