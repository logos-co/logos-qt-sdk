{
  description = "Logos Qt SDK - the Qt developer layer (LogosAPI, provider base classes, QObject provider glue) over logos-protocol";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    logos-protocol = {
      url = "github:logos-co/logos-protocol";
      inputs.logos-nix.follows = "logos-nix";
    };
    # The canonical, language-neutral LIDL frontend the qt-generator links.
    logos-lidl = {
      url = "github:logos-co/logos-lidl";
      inputs.logos-nix.follows = "logos-nix";
    };
    # Test-only: logos-cpp-generator is used to generate the provider
    # dispatch fixture exercised by test_provider_dispatch.
    logos-cpp-sdk = {
      url = "github:logos-co/logos-cpp-sdk";
      inputs.logos-nix.follows = "logos-nix";
      inputs.logos-protocol.follows = "logos-protocol";
      inputs.logos-lidl.follows = "logos-lidl";
    };
  };

  outputs = { self, nixpkgs, logos-nix, logos-protocol, logos-lidl, logos-cpp-sdk }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
        protocolLib = logos-protocol.packages.${system}.logos-protocol-lib;
        cppGenerator = logos-cpp-sdk.packages.${system}.cpp-generator;
        lidlPkg = logos-lidl.packages.${system}.logos-lidl;
      });

      # Same as forAllSystems, plus the "x86_64-windows" pseudo-system. This
      # cannot just be logos-nix.lib.forAllTargets, because that only supplies
      # { system, pkgs } and this flake also threads per-system dependencies
      # through.
      #
      # Keying the Windows target as a SYSTEM is what keeps
      # `dep.packages.${system}.foo` working untouched -- the dependency flakes
      # expose the same pseudo-system.
      windowsBuildSystem = "x86_64-linux";
      forAllTargets = f:
        nixpkgs.lib.genAttrs (systems ++ [ "x86_64-windows" ]) (system:
          let
            isWin = system == "x86_64-windows";
          in
          f {
            inherit system;
            pkgs =
              if isWin then logos-nix.lib.mkWindowsPkgs { buildSystem = windowsBuildSystem; }
              else import nixpkgs { inherit system; };

            # Target-side library: follows the target.
            protocolLib = logos-protocol.packages.${system}.logos-protocol-lib;
            lidlPkg = logos-lidl.packages.${system}.logos-lidl;

            # HOST TOOL: the code generator is executed during the build, so it
            # must be a native binary for the machine doing the building. Taking
            # it from packages.x86_64-windows would hand the Linux builder a PE
            # it cannot run -- the same rule that puts repc/moc in
            # QT_HOST_PATH rather than the target Qt.
            cppGenerator =
              logos-cpp-sdk.packages.${if isWin then windowsBuildSystem else system}.cpp-generator;
          });
    in
    {
      packages = forAllTargets ({ pkgs, protocolLib, cppGenerator, lidlPkg, ... }:
        let
          common = import ./nix/default.nix { inherit pkgs; };
          src = ./.;

          lib = import ./nix/lib.nix { inherit pkgs common src protocolLib; };
          qtGenerator = import ./nix/qt-generator.nix {
            inherit pkgs src;
            cppGeneratorBin = cppGenerator;
            logos-lidl = lidlPkg;
          };
          include = import ./nix/include.nix { inherit pkgs common src; };
          tests = import ./nix/tests.nix {
            inherit pkgs common src protocolLib cppGenerator qtGenerator;
          };

          qtSdk = pkgs.symlinkJoin {
            name = "logos-qt-sdk";
            paths = [ lib include ];
            propagatedBuildInputs = common.propagatedBuildInputs;
          };
        in
        {
          logos-qt-sdk-lib = lib;
          logos-qt-sdk-include = include;
          inherit tests;

          logos-qt-sdk = qtSdk;
          logos-qt-generator = qtGenerator;
          default = qtSdk;
        }
      );

      checks = forAllSystems ({ pkgs, protocolLib, cppGenerator, lidlPkg }:
        let
          common = import ./nix/default.nix { inherit pkgs; };
          src = ./.;
          qtGenerator = import ./nix/qt-generator.nix {
            inherit pkgs src;
            cppGeneratorBin = cppGenerator;
            logos-lidl = lidlPkg;
          };
          tests = import ./nix/tests.nix {
            inherit pkgs common src protocolLib cppGenerator qtGenerator;
          };
        in
        {
          inherit tests;
        }
      );

      devShells = forAllSystems ({ pkgs, protocolLib, cppGenerator, lidlPkg }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtremoteobjects
            pkgs.gtest
            pkgs.boost
            pkgs.openssl
            pkgs.nlohmann_json
            protocolLib
            cppGenerator
          ];
          shellHook = ''
            export LOGOS_PROTOCOL_ROOT="${protocolLib}"
          '';
        };
      });
    };
}
