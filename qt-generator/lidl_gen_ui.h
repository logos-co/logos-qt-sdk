// UI-backend glue generation: type=ui_qml + interface=universal modules.
//
// USER-WRITTEN: the .rep (the view contract) and the *Backend.{h,cpp}
// deriving <RepClass>SimpleSource + LogosModuleContext.
// GENERATED: <name>_ui_interface.h (PluginInterface + IID) and
// <name>_ui_glue.{h,cpp} (the *Plugin: Q_PLUGIN_METADATA + initLogos wiring
// LogosModules/context into the backend + setBackend).
#pragma once

#include <QString>

struct UiGlueSpec {
    QString moduleName;     // metadata.json name (e.g. ticker_panel)
    QString moduleVersion;  // metadata.json version
    QString pluginBase;     // PascalCase of moduleName (class name stem)
    QString repClass;       // class declared in the user's .rep
    QString backendClass;   // user's backend class (default <pluginBase>Backend)
    QString backendHeader;  // include name (default <name>_backend.h)
};

bool lidlUiParseRepClass(const QString& repPath, QString* repClass, QString* whyNot);
QString lidlMakeUiInterfaceHeader(const UiGlueSpec& spec);
QString lidlMakeUiGlueHeader(const UiGlueSpec& spec);
QString lidlMakeUiGlueSource(const UiGlueSpec& spec);
