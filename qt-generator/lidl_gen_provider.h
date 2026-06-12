#ifndef LIDL_GEN_PROVIDER_H
#define LIDL_GEN_PROVIDER_H

#include "lidl_ast.h"
#include <QString>
#include <QTextStream>

// Map a LIDL TypeExpr to the C++ std type string used in the pure implementation class
QString lidlTypeToStd(const TypeExpr& te);

// True if this type can be represented as a pure C++ std type (no Qt)
bool lidlIsStdConvertible(const TypeExpr& te);

// Generate the Qt glue header (plugin class + provider object with Q_INVOKABLE wrappers)
QString lidlMakeProviderHeader(const ModuleDecl& module,
                               const QString& implClass,
                               const QString& implHeader);

// Generate callMethod() + getMethods() dispatch source
QString lidlMakeProviderDispatch(const ModuleDecl& module);

// Generate the `<name>_events.cpp` source: Qt-MOC-style definitions of
// methods declared in the impl's `logos_events:` section. Each body
// marshals typed args into a QVariantList and calls
// LogosModuleContext::emitEventImpl_, which the provider's onInit wires
// to the QRO wire via LogosProviderBase::emitEvent.
QString lidlMakeEventsSource(const ModuleDecl& module,
                              const QString& implClass,
                              const QString& implHeader);

// Full pipeline: parse .lidl, generate provider glue + dispatch + metadata
// Returns 0 on success, non-zero on error
int lidlGenerateProviderGlue(const QString& lidlPath,
                              const QString& implClass,
                              const QString& implHeader,
                              const QString& outputDir,
                              QTextStream& out, QTextStream& err);

#endif // LIDL_GEN_PROVIDER_H
