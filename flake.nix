{
  description = "Logos Qt SDK - the Qt developer layer (LogosAPI, provider base classes, QObject provider glue) over logos-protocol";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    # Unpinned: feat/per-client-token-store merged (logos-protocol#59), so
    # TokenManager::forIdentity / isolateIdentity and the host-services C ABI
    # (lp_grant_host_services, lp_token_keys) that the re-exported Qt host
    # runtime needs are on master. That PR was squash-merged, so the old pin
    # c8bab12 is not an ancestor of master even though its content is in it —
    # check the FILES, not the ancestry, when retiring one of these pins.
    logos-protocol = {
      url = "github:logos-co/logos-protocol";
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
    # Unpinned again: feat/b4-qt-host-windows-target merged (logos-plugin-qt#19),
    # so `logos-qt-host` is on that repo's master and an unpinned input resolves
    # it. The three `follows` above stay — they are what keeps one logos-protocol
    # in the closure, and that is independent of pinning.
    logos-plugin-qt = {
      url = "github:logos-co/logos-plugin-qt";
      inputs.logos-nix.follows = "logos-nix";
      inputs.logos-protocol.follows = "logos-protocol";
      inputs.logos-lidl.follows = "logos-lidl";
    };
    # Two things, neither of them a generated test fixture any more: the
    # `cpp-generator` package ships the shared LIDL frontend sources that
    # logos-qt-generator compiles (share/lidl-frontend, see nix/qt-generator.nix),
    # and `logos-cpp-include` supplies the Qt-free headers this SDK's host
    # veneer and the test suite's single-TU compile check include.
    #
    # It used to also generate tests/qt-sdk's provider-dispatch fixture via
    # `logos-cpp-generator --provider-header`; that flag, and the
    # `interface: "provider"` authoring path behind it, were removed.
    # Unpinned again: feat/sdk-codegen-b3-d11 merged (cpp-sdk#138), so master
    # now ships logos_host_core.h and the rest of the capability split that
    # cpp/CMakeLists.txt requires. The rev pin existed only to bridge that gap.
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

            # The Qt host runtime, taken from logos-plugin-qt for EVERY target
            # including Windows. That repo keys `packages` by forAllTargets, so
            # packages.x86_64-windows.logos-qt-host is a real mingw build there.
            # This flake used to carry nix/qt-host-windows.nix, a second recipe
            # over the same sources, because plugin-qt published the unix systems
            # only; logos-plugin-qt#19 added the Windows target and that file
            # said to delete it the moment it did.
            qtHost = logos-plugin-qt.packages.${system}.logos-qt-host;

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

          lib = import ./nix/lib.nix { inherit pkgs common src protocolLib qtHost cppSdkInclude; };
          qtGenerator = import ./nix/qt-generator.nix {
            inherit pkgs src;
            cppGeneratorBin = cppGenerator;
            logos-lidl = lidlPkg;
          };
          include = import ./nix/include.nix { inherit pkgs common src; };
          tests = import ./nix/tests.nix {
            inherit pkgs common src protocolLib cppGenerator cppSdkInclude qtGenerator qtHost;
          };

          # DO NOT add propagatedBuildInputs to this join. It is INERT, and the
          # comment that used to sit here claimed the opposite -- it described a
          # real hazard beside code that did not prevent it, which reads to the
          # next person as "handled".
          #
          # Why it is inert, from nixpkgs' stdenv setup:
          #
          #   genericBuild() {
          #     if [ -f "${buildCommandPath:-}" ]; then source "$buildCommandPath"; return; fi
          #     if [ -n "${buildCommand:-}" ];     then eval   "$buildCommand";     return; fi
          #     definePhases
          #     for curPhase in ${phases[*]}; do runPhase "$curPhase"; done
          #   }
          #
          # symlinkJoin is a buildCommand derivation, so genericBuild RETURNS
          # before definePhases -- and fixupPhase, the ONLY writer of
          # nix-support/propagated-build-inputs, never runs. The same is true of
          # runCommand, buildEnv and writeShellApplication. What consumers
          # actually read here is `lib`'s copy of that file, lndir'd in; the
          # attribute declared on the join is not even a reference. Measured
          # three ways: declaring `dep` on a join whose paths propagate zlib
          # yields zlib and not dep; with no nix-support in any path, $out has no
          # nix-support at all; and when two paths both propagate, lndir drops
          # the collision and the SECOND one is silently discarded.
          #
          # So propagation is decided in nix/lib.nix, which is where qtHost was
          # removed from -- see the header there for what that export broke.
          # If this package ever genuinely needs to propagate something, declare
          # it on a real mkDerivation among `paths` (order matters, first wins)
          # or write the file in postBuild; do not declare it here.
          qtSdk = pkgs.symlinkJoin {
            name = "logos-qt-sdk";
            paths = [ lib include ];
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

      checks = forAllSystems ({ pkgs, system, protocolLib, cppGenerator, cppSdkInclude, lidlPkg, qtHost, ... }:
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

          # This package must not export a logos-qt-host IDENTITY -- see
          # nix/checks/no-qt-host-export.nix for what that broke and why the
          # assertion is a scan rather than three targeted greps. Run in CI,
          # not merely exposed here.
          no-qt-host-export = import ./nix/checks/no-qt-host-export.nix {
            inherit pkgs;
            subject = self.packages.${system}.logos-qt-sdk;
          };
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
