# Installs the logos-qt-sdk headers AND sources in the source-export layout
# ($out/include/cpp/...), mirroring logos-cpp-sdk's historical shipping
# shape so build systems that compile SDK sources directly (source-layout
# plugin builds) keep working.
{ pkgs, common, src }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-headers";
  version = common.version;

  inherit src;
  inherit (common) meta;

  dontBuild = true;
  dontConfigure = true;

  installPhase = ''
    runHook preInstall

    mkdir -p $out/include/cpp
    mkdir -p $out/include/core
    for file in cpp/*.h cpp/*.cpp; do
      cp "$file" $out/include/cpp/
    done
    cp core/interface.h $out/include/core/

    runHook postInstall
  '';
}
