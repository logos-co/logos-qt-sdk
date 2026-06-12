{
  description = "Logos Qt SDK - the Qt developer layer (LogosAPI, provider base classes, QObject provider glue) over logos-protocol";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    logos-protocol = {
      url = "github:logos-co/logos-protocol";
      inputs.logos-nix.follows = "logos-nix";
    };
    # Test-only: logos-cpp-generator is used to generate the provider
    # dispatch fixture exercised by test_provider_dispatch.
    logos-cpp-sdk = {
      url = "github:logos-co/logos-cpp-sdk";
      inputs.logos-nix.follows = "logos-nix";
      inputs.logos-protocol.follows = "logos-protocol";
    };
  };

  outputs = { self, nixpkgs, logos-nix, logos-protocol, logos-cpp-sdk }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
        protocolLib = logos-protocol.packages.${system}.logos-protocol-lib;
        cppGenerator = logos-cpp-sdk.packages.${system}.cpp-generator;
      });
    in
    {
      packages = forAllSystems ({ pkgs, protocolLib, cppGenerator }:
        let
          common = import ./nix/default.nix { inherit pkgs; };
          src = ./.;

          lib = import ./nix/lib.nix { inherit pkgs common src protocolLib; };
          qtGenerator = import ./nix/qt-generator.nix {
            inherit pkgs src;
            cppGeneratorBin = cppGenerator;
          };
          include = import ./nix/include.nix { inherit pkgs common src; };
          tests = import ./nix/tests.nix { inherit pkgs common src protocolLib cppGenerator; };

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

      checks = forAllSystems ({ pkgs, protocolLib, cppGenerator }:
        let
          common = import ./nix/default.nix { inherit pkgs; };
          src = ./.;
          tests = import ./nix/tests.nix { inherit pkgs common src protocolLib cppGenerator; };
        in
        {
          inherit tests;
        }
      );

      devShells = forAllSystems ({ pkgs, protocolLib, cppGenerator }: {
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
