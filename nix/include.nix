# Installs logos-qt-sdk's OWN headers in the source-export layout
# ($out/include/cpp/...), mirroring logos-cpp-sdk's historical shipping shape so
# build systems that add that directory to the include path keep working.
#
# What this no longer ships, and why:
#
#   The five .cpp files. They were here for the "source layout" branches in
#   logos-module-builder / logos-test-framework / logos-basecamp, which compile
#   the Qt developer layer straight into the consumer instead of linking it.
#   Those sources are logos-qt-host's now, and every one of those branches is
#   guarded by LOGOS_QT_SDK_IS_SOURCE, which is FALSE for an installed prefix --
#   i.e. false for every Nix build, which is all of them.
#
#   The five moved headers and core/interface.h. They are logos-qt-host's, and
#   this prefix does not re-export them under any name: a consumer that needs
#   LogosAPI takes logos-qt-host's include directory, which arrives through
#   logos-qt-sdk::logos_qt_sdk's link interface. `cp cpp/*.h` below is therefore
#   exactly this SDK's own three headers -- logos_ui_plugin_context.h,
#   logos_qt_lp_bridge.h, logos_qt_wire.h -- and stays that way as long as
#   cpp/ holds only what this SDK owns.
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
    cp cpp/*.h $out/include/cpp/

    runHook postInstall
  '';
}
