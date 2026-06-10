# Builds and runs the test suite
{ pkgs, common, src, protocolLib, cppGenerator }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-tests";
  version = common.version;

  inherit src;

  nativeBuildInputs = common.nativeBuildInputs ++ [ cppGenerator ];
  buildInputs = common.buildInputs ++ [ pkgs.gtest protocolLib ];
  cmakeFlags = common.cmakeFlags;

  dontUseCmakeConfigure = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p build-tests
    cd build-tests
    cmake ../tests -GNinja -DLOGOS_PROTOCOL_ROOT=${protocolLib} $cmakeFlags
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
