{
  description = "Logos Qt SDK - the Qt developer layer (LogosAPI, provider base classes, QObject provider glue) over logos-protocol";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    # Pinned by rev for the same reason logos-plugin-qt is, below: the Qt host
    # runtime this SDK re-exports calls TokenManager::forIdentity/isolateIdentity,
    # and those are on feat/per-client-token-store — NOT on logos-protocol's
    # master. An unpinned url would lock onto master and logos-qt-host would not
    # compile. c8bab12 is a fast-forward from master (it contains e6d5b57), so
    # nothing on master is given up by pinning it. Drop the rev once it merges.
    logos-protocol = {
      url = "github:logos-co/logos-protocol/c8bab12834dbf92155b483546875e6078d17c74e";
      inputs.logos-nix.follows = "logos-nix";
    };
    # The canonical, language-neutral LIDL frontend the qt-generator links.
    logos-lidl = {
      url = "github:logos-co/logos-lidl";
      inputs.logos-nix.follows = "logos-nix";
    };
    # Where the Qt host runtime lives now: logos-plugin-qt's `logos-qt-host`
    # package owns LogosAPI, LogosAPIProvider, LogosProviderBase, the QObject
    # adapter and the Qt argument decoder. This SDK re-exports it.
    #
    # The three `follows` are load-bearing, not tidiness. logos-qt-host links
    # logos-protocol, and so does this SDK; if the two resolved to different
    # logos-protocol revisions the closure would carry two TokenManagers, two
    # transport registries and two of every other function-local static in
    # there — the exact split-brain the Windows single-provider work spent
    # itself closing, reintroduced through the lock file instead of the linker.
    #
    # Pinned by rev, unlike every other input here, because the commit that
    # introduces logos-qt-host has not reached logos-plugin-qt's master yet —
    # an unpinned input would lock onto a master with no such package and this
    # flake would not evaluate. The workspace pins the same rev. Drop the rev
    # once it merges.
    logos-plugin-qt = {
      url = "github:logos-co/logos-plugin-qt/8ccb1fc81642ee52e843b69ac3f90a1ec7084299";
      inputs.logos-nix.follows = "logos-nix";
      inputs.logos-protocol.follows = "logos-protocol";
      inputs.logos-lidl.follows = "logos-lidl";
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

  outputs = { self, nixpkgs, logos-nix, logos-protocol, logos-lidl, logos-cpp-sdk, logos-plugin-qt }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
        protocolLib = logos-protocol.packages.${system}.logos-protocol-lib;
        cppGenerator = logos-cpp-sdk.packages.${system}.cpp-generator;
        # Headers only — the base SDK's include set, needed by the test suite's
        # single-TU compile check (see nix/tests.nix). Not part of any shipped
        # package here: this SDK's own sources do not include them.
        cppSdkInclude = logos-cpp-sdk.packages.${system}.logos-cpp-include;
        lidlPkg = logos-lidl.packages.${system}.logos-lidl;
        qtHost = logos-plugin-qt.packages.${system}.logos-qt-host;
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
            pkgs =
              if isWin then logos-nix.lib.mkWindowsPkgs { buildSystem = windowsBuildSystem; }
              else import nixpkgs { inherit system; };
            protocolLib = logos-protocol.packages.${system}.logos-protocol-lib;
          in
          f {
            inherit system pkgs;

            # Target-side library: follows the target.
            inherit protocolLib;
            lidlPkg = logos-lidl.packages.${system}.logos-lidl;

            # The Qt host runtime. logos-plugin-qt publishes it for the unix
            # systems only, so the Windows target builds the same sources here
            # instead — see nix/qt-host-windows.nix for why that is a duplicated
            # recipe and not a duplicated archive.
            qtHost =
              if isWin
              then import ./nix/qt-host-windows.nix {
                inherit pkgs protocolLib;
                common = import ./nix/default.nix { inherit pkgs; };
                pluginQtSrc = logos-plugin-qt;
              }
              else logos-plugin-qt.packages.${system}.logos-qt-host;

            # HOST TOOL: the code generator is executed during the build, so it
            # must be a native binary for the machine doing the building. Taking
            # it from packages.x86_64-windows would hand the Linux builder a PE
            # it cannot run -- the same rule that puts repc/moc in
            # QT_HOST_PATH rather than the target Qt.
            cppGenerator =
              logos-cpp-sdk.packages.${if isWin then windowsBuildSystem else system}.cpp-generator;

            # Headers, so target-typed like every other include set here — but
            # architecture-independent in practice, since the package installs
            # sources and nothing else. Only the test suite consumes it.
            cppSdkInclude = logos-cpp-sdk.packages.${system}.logos-cpp-include;
          });
    in
    {
      packages = forAllTargets ({ pkgs, protocolLib, cppGenerator, cppSdkInclude, lidlPkg, qtHost, ... }:
        let
          common = import ./nix/default.nix { inherit pkgs; };
          src = ./.;

          lib = import ./nix/lib.nix { inherit pkgs common src protocolLib qtHost; };
          qtGenerator = import ./nix/qt-generator.nix {
            inherit pkgs src;
            cppGeneratorBin = cppGenerator;
            logos-lidl = lidlPkg;
          };
          include = import ./nix/include.nix { inherit pkgs common src; };
          tests = import ./nix/tests.nix {
            inherit pkgs common src protocolLib cppGenerator cppSdkInclude qtGenerator qtHost;
          };

          qtSdk = pkgs.symlinkJoin {
            name = "logos-qt-sdk";
            paths = [ lib include ];
            # qtHost is propagated by the JOIN as well as by `lib`, because
            # symlinkJoin does not inherit the propagated inputs of the paths it
            # joins and this attribute — not `lib` — is what consumers add to
            # their buildInputs. Without it a consumer's find_package(
            # logos-qt-sdk) would fall back to the Config's baked HINTS instead
            # of resolving logos-qt-host off CMAKE_PREFIX_PATH.
            propagatedBuildInputs = common.propagatedBuildInputs ++ [ qtHost ];
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

      checks = forAllSystems ({ pkgs, protocolLib, cppGenerator, cppSdkInclude, lidlPkg, qtHost, ... }:
        let
          common = import ./nix/default.nix { inherit pkgs; };
          src = ./.;
          qtGenerator = import ./nix/qt-generator.nix {
            inherit pkgs src;
            cppGeneratorBin = cppGenerator;
            logos-lidl = lidlPkg;
          };
          tests = import ./nix/tests.nix {
            inherit pkgs common src protocolLib cppGenerator cppSdkInclude qtGenerator qtHost;
          };
        in
        {
          inherit tests;
        }
      );

      devShells = forAllSystems ({ pkgs, protocolLib, cppGenerator, lidlPkg, qtHost, ... }: {
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
            qtHost
            cppGenerator
          ];
          shellHook = ''
            export LOGOS_PROTOCOL_ROOT="${protocolLib}"
            export LOGOS_QT_HOST_ROOT="${qtHost}"
          '';
        };
      });
    };
}
