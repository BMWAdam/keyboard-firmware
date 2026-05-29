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

    pico-examples = {
      url = "git+https://github.com/raspberrypi/pico-examples.git";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, freertos, openocd-src, pico-examples }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        guiPkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnsupportedSystem = true;
            allowBroken = true;
          };
        };

        pico-sdk-full = pkgs.pico-sdk.override { withSubmodules = true; };

        fwSrc = pkgs.runCommand "fw-src-with-pio" {} ''
          cp -r ${./src}/. $out
          chmod -R u+w $out
          cp ${pico-examples}/pio/ws2812/ws2812.pio $out/
        '';

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

          src = fwSrc;

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
            cp *.uf2 *.elf *.hex *.bin $out/ || echo "No binaries found to copy."
            cp compile_commands.json $out
          '';
        };

        guiSrc = ./gui;

        # Linux Native Build
        gui-linux = guiPkgs.stdenv.mkDerivation {
          pname = "gui-linux";
          version = "0.1.0";
          src = guiSrc;

          nativeBuildInputs = [ guiPkgs.cmake guiPkgs.qt6Packages.wrapQtAppsHook ];
          buildInputs = [ guiPkgs.qt6Packages.qtbase ];

          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=RelWithDebInfo" ];

          installPhase = ''
            mkdir -p $out/bin $out/debug

            cp gui $out/debug/linux.elf
            cp gui $out/bin/linux
            ${pkgs.stdenv.cc.bintools.targetPrefix}strip $out/bin/linux
          '';
        };

        gui-win =
          let
            mkStub = pname: pkgs_: pkgs_.stdenv.mkDerivation {
              inherit pname;
              version = "stub";
              dontUnpack = true;
              installPhase = "mkdir -p $out/lib $out/include $out/bin";
            };

            # Take the base Windows cross-compilation set and extend it safely.
            # Take the base Windows cross-compilation set and extend it safely.
            mingwPkgs = guiPkgs.pkgsCross.mingwW64.extend (final: prev:
              if prev.stdenv.hostPlatform.isWindows then {
                
                makeShellWrapper = final.stdenv.mkDerivation {
                  pname = "make-shell-wrapper-hook";
                  version = "stub";
                  dontUnpack = true;
                  installPhase = ''
                    mkdir -p $out/nix-support
                    echo "wrapProgram() { true; }" > $out/nix-support/setup-hook
                  '';
                };
                makeWrapper = final.makeShellWrapper;

                bash = mkStub "bash" final;
                bashInteractive = final.bash;
                
                runtimeShellPackage = final.bash;
                runtimeShell = "${final.bash}/bin/bash";
                
                libsysprof-capture = mkStub "libsysprof-capture" final;
                tcl                = mkStub "tcl"             final;
                expect             = mkStub "expect"          final;
                dejagnu            = mkStub "dejagnu"         final;

                # --- NEW STUBS FOR QT6 GUI ---
                libglvnd = mkStub "libglvnd" final;
                libX11   = mkStub "libX11"   final;
                libXext  = mkStub "libXext"  final;
                xorg = prev.xorg // {
                  libX11  = mkStub "libX11"  final;
                  libXext = mkStub "libXext" final;
                };
                # -----------------------------

                glib = prev.glib.overrideAttrs (old: {
                  buildInputs = builtins.filter (d: (d.pname or "") != "libsysprof-capture") (old.buildInputs or []);
                  mesonFlags = (old.mesonFlags or []) ++ [ "-Dsysprof=disabled" ];
                });

                sqlite = prev.sqlite.overrideAttrs (old: {
                  buildInputs = builtins.filter (d: (d.pname or "") != "tcl") (old.buildInputs or []);
                  configureFlags = builtins.filter (f: builtins.match "--.*tcl.*" f == null) (old.configureFlags or []) ++ [ "--disable-tcl" ];
                });

                python313 = prev.buildPackages.python313;
                python3 = prev.buildPackages.python3;
                python313Packages = prev.buildPackages.python313Packages;
                python3Packages = prev.buildPackages.python3Packages;
              } else { });
          in
          mingwPkgs.stdenv.mkDerivation {
            pname = "gui-win";
            version = "0.1.0";
            src = guiSrc;
            dontWrapQtApps = true;

            nativeBuildInputs = [ guiPkgs.cmake ];
            buildInputs = [ mingwPkgs.qt6.qtbase ];

            cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];

            installPhase = ''
              mkdir -p $out/bin
              cp gui.exe $out/bin/win.exe
            '';
          };

        # Combined Output
        gui-combined = pkgs.runCommand "gui-app" {} ''
          mkdir -p $out

          cp -a ${gui-linux}/bin/* $out/
          cp ${gui-linux}/debug/linux.elf $out/linux.elf
          cp ${gui-win}/bin/win.exe $out/win.exe
        '';
      in {
        packages = {
          default = fw;
          gui = gui-combined;
        };

        apps.flash = {
          type = "app";
          program = let
            flashScript = pkgs.writeShellScriptBin "flash-pico2w" ''
              PROJECT_ROOT="$PWD"
              while [ ! -f "$PROJECT_ROOT/flake.nix" ]; do
                if [ "$PROJECT_ROOT" = "/" ]; then
                  echo "Error: flake.nix not found in current or any parent directories!"
                  exit 1
                fi
                PROJECT_ROOT=$(dirname "$PROJECT_ROOT")
              done

              echo "Project root found at: $PROJECT_ROOT"

              cd "$PROJECT_ROOT"
              cd debug

              if [ ! -f "openocd.cfg" ]; then
                echo "Error: openocd.cfg not found!"
                exit 1
              fi

              echo "Building firmware..."
              nix build .

              if [ $? -ne 0 ]; then
                echo "Error: nix build failed!"
                exit 1
              fi

              ELF_FILE=$(ls ./result/*.elf | head -n 1)

              if [ -z "$ELF_FILE" ]; then
                echo "Error: No .elf file found in ./result/!"
                exit 1
              fi

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
            gdb
            picocom
          ];

          buildInputs = with pkgs; [
            pico-sdk-full
          ];

          shellHook = ''
            export FREERTOS_KERNEL_PATH="${freertos}"
            export PICO_SDK_PATH="${pico-sdk-full}/lib/pico-sdk"
            echo "Pico 2 W FreeRTOS Development Environment Loaded."
            echo "PICO_SDK_PATH explicitly set to: $PICO_SDK_PATH"
          '';
        };
      }
    );
}
