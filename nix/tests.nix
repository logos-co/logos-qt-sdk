# Builds and runs the test suite
#
# qtGenerator is a nativeBuildInput because tests/qt-generator drives the
# INSTALLED binary rather than linking the generator's internals — that binary
# is what module-builder actually invokes, so it is what the goldens must pin.
{ pkgs, common, src, protocolLib, cppGenerator, qtGenerator, qtHost }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-tests";
  version = common.version;

  inherit src;

  nativeBuildInputs = common.nativeBuildInputs ++ [ cppGenerator qtGenerator ];
  buildInputs = common.buildInputs ++ [ pkgs.gtest protocolLib qtHost ];
  cmakeFlags = common.cmakeFlags;

  dontUseCmakeConfigure = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p build-tests
    cd build-tests
    cmake ../tests -GNinja -DLOGOS_PROTOCOL_ROOT=${protocolLib} \
      -DLOGOS_QT_HOST_ROOT=${qtHost} $cmakeFlags
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
