# Installs the logos-qt-sdk headers AND sources in the source-export layout
# ($out/include/cpp/...), mirroring logos-cpp-sdk's historical shipping
# shape so build systems that compile SDK sources directly (source-layout
# plugin builds) keep working.
{ pkgs, common, src }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-headers";
  # qtbase\'s setup hook errors in qtPreHook unless a wrapper hook ran or
  # this is set; the wrapper hooks are absent on Windows (they cannot even
  # evaluate for a mingw host) and would skip a PE anyway.
  dontWrapQtApps = true;
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
