# Builds logos-qt-host for the "x86_64-windows" pseudo-system, out of the
# logos-plugin-qt SOURCE, with this repo's Windows-aware build settings.
#
# WHY THIS EXISTS. logos-qt-host is logos-plugin-qt's package and on every
# system that repo publishes it for, that package is the one this SDK links.
# But logos-plugin-qt exposes `packages` for the four unix systems only, so
# `packages.x86_64-windows.logos-qt-host` does not exist, and the Windows leg of
# this flake — which logos-liblogos consumes as
# `logos-qt-sdk.packages.x86_64-windows.default` — would not even evaluate.
# Its own build recipe cannot be reused as-is either: it takes
# qt6.wrapQtAppsNoGuiHook unconditionally (the wrapper hooks cannot evaluate for
# a mingw host) and passes no cross flags, so Qt's host tools — moc, repc —
# would be looked for among the target's PEs.
#
# What is duplicated here is the RECIPE, not the code: the sources are still
# logos-plugin-qt's, and nothing else can produce a Windows logos-qt-host, so
# there is still exactly one archive. That distinction is the whole point on
# Windows — PE has no symbol interposition, so a second archive is a second
# TokenManager singleton, which is the failure the single-provider scheme in
# logos-liblogos exists to prevent.
#
# Delete this file the moment logos-plugin-qt builds logos-qt-host for the
# Windows target itself, and take the package from there like every other
# system does.
{ pkgs, common, pluginQtSrc, protocolLib }:

pkgs.stdenv.mkDerivation {
  pname = "logos-qt-host";
  version = "0.1.0";

  src = pluginQtSrc;

  # qtbase's setup hook errors in qtPreHook unless a wrapper hook ran or this is
  # set; the wrapper hooks are absent on Windows and would skip a PE anyway.
  dontWrapQtApps = true;

  inherit (common) nativeBuildInputs cmakeFlags;
  buildInputs = common.buildInputs ++ [ protocolLib ];
  propagatedBuildInputs = common.propagatedBuildInputs ++ [ protocolLib ];

  dontUseCmakeConfigure = true;

  # The CMake project is cpp/, but the source tree must be the repo root:
  # cpp/CMakeLists.txt installs ../core/interface.h alongside its own headers.
  buildPhase = ''
    runHook preBuild

    mkdir -p build-qt-host
    cd build-qt-host
    cmake ../cpp -GNinja -DCMAKE_INSTALL_PREFIX=$out \
      -DLOGOS_PROTOCOL_ROOT=${protocolLib} $cmakeFlags
    ninja
    cd ..

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    cmake --install build-qt-host
    runHook postInstall
  '';

  meta = with pkgs.lib; {
    description = "Logos Qt host runtime (Windows cross-build of logos-plugin-qt's logos-qt-host)";
    platforms = platforms.windows;
  };
}
