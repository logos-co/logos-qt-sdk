# Builds the logos-qt-sdk CMake package config
#
# There is no archive here any more. The five translation units this used to
# compile live in logos-qt-host (logos-plugin-qt); `logos_qt_sdk` is an
# INTERFACE target that links it, so what this derivation installs is the
# package config, the export set and logos_ui_plugin_context.h. qtHost is a
# propagated input so a consumer that only asks for logos-qt-sdk still gets
# logos-qt-host's prefix on CMAKE_PREFIX_PATH and its include dir on the
# compiler's search path.
{ pkgs, common, src, protocolLib, qtHost, cppSdkInclude }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-lib";
  # qtbase\'s setup hook errors in qtPreHook unless a wrapper hook ran or
  # this is set; the wrapper hooks are absent on Windows (they cannot even
  # evaluate for a mingw host) and would skip a PE anyway.
  dontWrapQtApps = true;
  version = common.version;

  inherit src;
  inherit (common) nativeBuildInputs cmakeFlags meta;
  buildInputs = common.buildInputs ++ [ protocolLib qtHost ];

  propagatedBuildInputs = common.propagatedBuildInputs ++ [ protocolLib qtHost ];

  dontUseCmakeConfigure = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p build-qt-sdk
    cd build-qt-sdk
    cmake ../cpp -GNinja -DCMAKE_INSTALL_PREFIX=$out \
      -DLOGOS_PROTOCOL_ROOT=${protocolLib} \
      -DLOGOS_QT_HOST_ROOT=${qtHost} \
      -DLOGOS_CPP_SDK_ROOT=${cppSdkInclude} $cmakeFlags
    ninja
    cd ..

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    cmake --install build-qt-sdk
    runHook postInstall
  '';
}
