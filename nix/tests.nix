# Builds and runs the test suite
#
# qtGenerator is a nativeBuildInput because tests/qt-generator drives the
# INSTALLED binary rather than linking the generator's internals — that binary
# is what module-builder actually invokes, so it is what the goldens must pin.
#
# cppSdkInclude is a buildInput (headers only, no library) for one target:
# tests/qt-generator's single-TU compile check. A generated consumer wrapper
# includes headers from BOTH layers — logos_api.h / logos_types.h from the Qt
# host, logos_async_result.h from the base SDK — so without it the suite can
# generate wrapper text but never hand it to a compiler, which is exactly how a
# wrapper that does not compile in the umbrella's TU got shipped.
{ pkgs, common, src, protocolLib, cppGenerator, cppSdkInclude, qtGenerator, qtHost }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-tests";
  version = common.version;

  inherit src;

  nativeBuildInputs = common.nativeBuildInputs ++ [ cppGenerator qtGenerator ];
  buildInputs = common.buildInputs ++ [ pkgs.gtest protocolLib qtHost cppSdkInclude ];
  cmakeFlags = common.cmakeFlags;

  dontUseCmakeConfigure = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p build-tests
    cd build-tests
    cmake ../tests -GNinja -DLOGOS_PROTOCOL_ROOT=${protocolLib} \
      -DLOGOS_QT_HOST_ROOT=${qtHost} -DLOGOS_CPP_SDK_ROOT=${cppSdkInclude} $cmakeFlags
    ninja
    cd ..

    runHook postBuild
  '';

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    cd build-tests
    export QT_QPA_PLATFORM=offscreen
    ctest --output-on-failure
    cd ..
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp build-tests/qt-sdk/qt_sdk_tests $out/bin/

    runHook postInstall
  '';

  inherit (common) meta;
}
