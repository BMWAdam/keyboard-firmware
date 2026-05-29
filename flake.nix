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

    nixpkgs-stable.url = "github:NixOS/nixpkgs/nixos-24.11";

    qmlmaterial = {
      url = "github:hypengw/QmlMaterial";
      flake = false;
    };

    nix-colors.url = "github:misterio77/nix-colors";
  };

  outputs = { self, nixpkgs, flake-utils, freertos, openocd-src, pico-examples, nixpkgs-stable, qmlmaterial, nix-colors }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        scheme = nix-colors.colorSchemes.tokyo-night-dark;

        c = scheme.palette;

        appColors = {
          surface        = "#${c.base00}";
          surfaceVariant = "#${c.base01}";
          onSurface      = "#${c.base05}";
          onSurfaceVar   = "#${c.base04}";
          primary        = "#${c.base0D}";
          secondary      = "#${c.base0E}";
          outline        = "#${c.base03}";

          surfaceLight        = "#${c.base07}";
          surfaceVariantLight = "#${c.base06}";
          onSurfaceLight      = "#${c.base00}";
          onSurfaceVarLight   = "#${c.base01}";
          primaryLight        = "#${c.base0D}";
          secondaryLight      = "#${c.base0E}";
          outlineLight        = "#${c.base04}";
        };

        # Write colors.json next to the binary at build time
        colorsJson = pkgs.writeText "colors.json" (builtins.toJSON appColors);

        pkgs = nixpkgs.legacyPackages.${system};
        pkgs-stable = nixpkgs-stable.legacyPackages.${system};

        agaveFont = pkgs.nerd-fonts.agave;

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

        qmlMaterialPkg = pkgs.stdenv.mkDerivation {
          pname = "qml-material";
          version = "git";

          src = qmlmaterial;
          dontWrapQtApps = true;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.pkg-config
            pkgs.qt6.qtbase
            pkgs.qt6.qtdeclarative
            pkgs.autoPatchelfHook
          ];

          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtdeclarative
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_SKIP_BUILD_RPATH=ON"
          ];

          installPhase = ''
            mkdir -p $out/qml
            cp -r qml_modules/* $out/qml/
            find . -name "libqml_material.so" -exec cp -n {} $out/qml/Qcm/Material/ \; || true
          '';
        };

        guiSrc = ./gui;

        gui-linux = guiPkgs.stdenv.mkDerivation {
          pname = "gui-linux";
          version = "0.1.0";
          src = guiSrc;

          nativeBuildInputs = [
            guiPkgs.cmake
            guiPkgs.qt6Packages.wrapQtAppsHook
            pkgs.tree
          ];

          buildInputs = [
            guiPkgs.qt6Packages.qtbase
            guiPkgs.qt6Packages.qtdeclarative
            guiPkgs.qt6Packages.qtserialport
            qmlMaterialPkg
            agaveFont
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
          ];

          qtWrapperArgs = [
            "--prefix" "QML2_IMPORT_PATH" ":" "${qmlMaterialPkg}/qml"
            "--set" "QT_QPA_FONTDIR" "$out/share/fonts"
          ];

          installPhase = ''
            mkdir -p $out/bin $out/share/fonts
            cp gui $out/bin/linux
            cp -r app $out/bin/
            cp ${colorsJson} $out/bin/colors.json
            cp -r ${agaveFont}/share/fonts/truetype/NerdFonts/. $out/share/fonts/
            cp compile_commands.json $out
          '';
        };

        gui-linux-appimage = pkgs.stdenv.mkDerivation {
          pname = "gui-linux-appimage";
          version = "0.1.0";
          dontUnpack = true;

          nativeBuildInputs = [ pkgs-stable.appimagekit ];

          installPhase = ''
            mkdir -p $out/bin AppDir/usr/bin
            cp -a ${gui-linux}/bin/. AppDir/usr/bin/

            cat > AppDir/AppRun <<'EOF'
            #!/bin/sh
            HERE="$(dirname "$(readlink -f "''${0}")")"
            export PATH="''${HERE}/usr/bin:''${PATH}"
            exec "''${HERE}/usr/bin/linux" "$@"
            EOF
            chmod +x AppDir/AppRun

            cat > AppDir/gui.desktop <<'EOF'
            [Desktop Entry]
            Type=Application
            Name=Pico GUI
            Exec=linux
            Icon=gui
            Categories=Utility;
            EOF

            touch AppDir/gui.png

            export APPIMAGE_EXTRACT_AND_RUN=1
            appimagetool AppDir $out/bin/linux.AppImage
          '';
        };

        gui-combined = pkgs.runCommand "gui-app" {} ''
          mkdir -p $out

          mkdir -p $out/qml

          cp -a ${gui-linux}/bin/. $out/
          chmod +w $out

          mkdir -p $out/bin
          cp -a ${gui-linux}/bin/linux $out/bin/gui-app

          mkdir -p $out/qml
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
