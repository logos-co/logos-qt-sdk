# Common build configuration shared across all packages
{ pkgs }:

{
  pname = "logos-qt-sdk";
  version = "0.1.0";

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.qt6.wrapQtAppsNoGuiHook
  ];

  # Qt is this SDK's whole point — it is the layer where Qt types live.
  buildInputs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtremoteobjects
    pkgs.boost
    pkgs.openssl
    pkgs.nlohmann_json
  ];

  # Same propagation policy as logos-protocol / logos-cpp-sdk: Qt excluded
  # (setup-hook ordering), the plain transport's deps carried for
  # find_dependency resolution in consumers.
  propagatedBuildInputs = [
    pkgs.boost
    pkgs.openssl
    pkgs.nlohmann_json
  ];

  cmakeFlags = [ "-GNinja" ];

  meta = with pkgs.lib; {
    description = "Logos Qt SDK - Qt developer layer over logos-protocol";
    platforms = platforms.unix;
  };
}
