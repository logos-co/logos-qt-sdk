# Builds the logos-qt-sdk static library + CMake package config
{ pkgs, common, src, protocolLib }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-lib";
  version = common.version;

  inherit src;
  inherit (common) nativeBuildInputs cmakeFlags meta;
  buildInputs = common.buildInputs ++ [ protocolLib ];

  propagatedBuildInputs = common.propagatedBuildInputs ++ [ protocolLib ];

  dontUseCmakeConfigure = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p build-qt-sdk
    cd build-qt-sdk
    cmake ../cpp -GNinja -DCMAKE_INSTALL_PREFIX=$out \
      -DLOGOS_PROTOCOL_ROOT=${protocolLib} $cmakeFlags
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
