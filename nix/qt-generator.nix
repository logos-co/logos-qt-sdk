# Builds the logos-qt-generator binary — the Qt CONSUMER glue and the ui
# backend glue. (The Qt-plugin/provider half is logos-plugin-qt's
# qt-host-generator.) Links logos-lidl for the
# canonical frontend and compiles the shared C++/Qt backend helpers
# distributed by logos-cpp-sdk (share/lidl-frontend) so both generators parse
# one surface.
{ pkgs, src, cppGeneratorBin, logos-lidl }:

pkgs.stdenv.mkDerivation {
  pname = "logos-qt-generator";
  # qtbase\'s setup hook errors in qtPreHook unless a wrapper hook ran or
  # this is set; the wrapper hooks are absent on Windows (they cannot even
  # evaluate for a mingw host) and would skip a PE anyway.
  dontWrapQtApps = true;
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

  dontWrapQtApps = true;
  buildInputs = [ pkgs.qt6.qtbase logos-lidl ];

  cmakeFlags = [
    "-GNinja"
    "-DLIDL_FRONTEND_DIR=${cppGeneratorBin}/share/lidl-frontend"
  ];
}
