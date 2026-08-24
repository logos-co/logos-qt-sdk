# Builds the logos-qt-sdk CMake package config
#
# There is no archive here any more. The five translation units this used to
# compile live in logos-qt-host (logos-plugin-qt); `logos_qt_sdk` is an
# INTERFACE target that links it (alongside the narrow capability targets
# logos_qt_common / logos_qt_consumer / logos_qt_host_core,
# which are in the same export set), so what this derivation installs is the
# package config, the export set and logos_qt_host_core.h.
#
# qtHost is a BUILD input and deliberately NOT a propagated one. It used to be
# propagated, on the theory that a consumer asking only for logos-qt-sdk should
# still get logos-qt-host's prefix on CMAKE_PREFIX_PATH. That is the line that
# made this package export an IDENTITY: the qt-host propagated here is the one
# THIS derivation was built against, and it landed on the consumer's
# CMAKE_PREFIX_PATH ahead of the consumer's own. A consumer therefore compiled
# and linked a qt-host it never chose, silently, with a green build -- and when
# logos-qt-host gained Q_INVOKABLE currentCallerJson, logos_host_qt could not
# see it: QMetaObject::invokeMethod failed at RUNTIME with "No such method" in
# every module process, collapsing current_caller() to {"kind":"unknown"}
# fleet-wide.
#
# This is the file that decided it. logos-qt-sdk is a symlinkJoin over this
# derivation, and symlinkJoin's own propagatedBuildInputs attribute is INERT
# (see flake.nix), so nix-support/propagated-build-inputs in the join is an
# lndir symlink to the one THIS line writes. Deleting the join's attribute
# changes nothing; deleting qtHost from the line below is the fix.
#
# Consumers now name logos-qt-host themselves -- LOGOS_QT_HOST_ROOT, then
# find_package(logos-qt-host ... NO_DEFAULT_PATH) BEFORE any find_package that
# could resolve one for them. logos-qt-sdkConfig.cmake asks for it by name via
# find_dependency, so a consumer that forgot fails at configure time naming the
# package it is missing, rather than building green against the wrong one.
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

  # NO qtHost here -- see the header. protocolLib stays: logos-qt-sdkConfig.cmake
  # calls find_dependency(logos-protocol) and every consumer already agrees on
  # one logos-protocol through `follows`, so it does not carry the same split.
  propagatedBuildInputs = common.propagatedBuildInputs ++ [ protocolLib ];

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
