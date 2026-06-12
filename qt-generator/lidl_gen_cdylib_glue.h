// The Qt half of the cdylib module path: the uniform Qt-plugin glue that
// wraps the (language-agnostic) module-impl C ABI. The Qt-FREE half — the
// C-ABI impl-exports around a C++ impl class — stays with logos-cpp-sdk's
// generator; this glue is emitted by logos-qt-generator.
#pragma once

#include <QString>
#include "lidl_ast.h"

QString lidlMakeCdylibGlueHeader(const ModuleDecl& module);
QString lidlMakeCdylibGlueSource(const ModuleDecl& module);
