#ifndef LIDL_GEN_QT_CONSUMER_H
#define LIDL_GEN_QT_CONSUMER_H

// The Qt-typed CONSUMER wrapper — `<Dep>`/`<Iface>` classes with QString /
// QByteArray / QVariantList / LogosResult / qlonglong signatures, the surface
// every hand-written Qt module already calls.
//
// It lives here, in the repo that owns Qt, and it is a VENEER: the class keeps
// its Qt types and converts at the edge, then delegates to the same
// logos-protocol C ABI (lp_*) path the std-typed wrapper uses. There is one
// transport path, one codec and one Qt type mapper underneath — where there
// used to be two parallel consumer implementations whose tables disagreed.
//
// Conversions are NOT emitted. Every generated body calls
// `logos::qt::toWire` / `logos::qt::fromWire<T>` (logos-qt-sdk's
// logos_qt_lp_bridge.h), which are one-line forwards to the canonical
// converters in logos-protocol's logos_json_convert. A missing conversion is
// added there, never inlined into this generator — otherwise the generator
// becomes a third copy of the conversion knowledge, which is the exact problem
// this replaces.

#include "lidl_compat.h"
#include <QString>

// Whether the emitted wrapper targets ONE fixed module or binds at runtime.
//   Static — `<Class>(LogosAPI*)`, the module name baked into every call.
//            What a concrete `dependencies` entry generates.
//   Bound  — `<Class>(LogosAPI*, const QString& moduleName)`, the name held in
//            `m_moduleName`. What an `interface_dependencies` entry generates,
//            reached through the umbrella's `bind_<name>(...)`.
enum class QtConsumerBind { Static, Bound };

// `moduleName` is the DEPENDENCY/INTERFACE name the umbrella uses (not
// necessarily `module.name` from the contract): it names the call target in
// Static mode and only the files/class in Bound mode. `className` is the
// wrapper class.
QString lidlMakeQtConsumerHeader(const ModuleDecl& module,
                                 const QString& moduleName,
                                 const QString& className,
                                 QtConsumerBind bind);

QString lidlMakeQtConsumerSource(const ModuleDecl& module,
                                 const QString& moduleName,
                                 const QString& className,
                                 const QString& headerBaseName,
                                 QtConsumerBind bind);

#endif  // LIDL_GEN_QT_CONSUMER_H
