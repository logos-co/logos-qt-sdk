# Builds the logos-qt-generator binary — ALL Qt glue emission (universal
# qt glue, the cdylib glue half, ui backend glue). Compiles the shared LIDL
# frontend distributed by logos-cpp-sdk (share/lidl-frontend) so both
# generators parse one surface.
{ pkgs, src, cppGeneratorBin }:

pkgs.stdenv.mkDerivation {
  pname = "logos-qt-generator";
  version = "0.1.0";
  inherit src;
  # The unpacked store name varies (path: flakes get hash-prefixed
  # names); resolve the subdir by glob instead of hardcoding "source".
  setSourceRoot = "sourceRoot=$(echo */qt-generator)";

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.qt6.wrapQtAppsNoGuiHook
  ];
  buildInputs = [ pkgs.qt6.qtbase ];

  cmakeFlags = [
    "-GNinja"
    "-DLIDL_FRONTEND_DIR=${cppGeneratorBin}/share/lidl-frontend"
  ];
}
