// UI-backend glue generation: type=ui_qml + interface=universal modules.
// From a parsed impl header (ModuleDecl), emits:
//   <name>.rep             — derived QtRO contract (the view API)
//   <name>_ui_interface.h  — PluginInterface subclass + IID
//   <name>_ui_glue.{h,cpp} — plugin deriving the repc SimpleSource +
//                            ViewPluginBase; forwards slots to the impl and
//                            wires LogosModules/context in initLogos.
#pragma once

#include <QString>
#include "lidl_ast.h"

bool lidlUiSupported(const ModuleDecl& module, QString* whyNot);
QString lidlMakeUiRepFile(const ModuleDecl& module);
QString lidlMakeUiInterfaceHeader(const ModuleDecl& module);
QString lidlMakeUiGlueHeader(const ModuleDecl& module,
                             const QString& implClass,
                             const QString& implHeader);
QString lidlMakeUiGlueSource(const ModuleDecl& module,
                             const QString& implClass);
