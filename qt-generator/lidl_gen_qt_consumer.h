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

// Reject what this path cannot express. Today that is exactly one thing: an
// optional RETURN, whose empty inhabitant is indistinguishable here from a
// failed call. Fills *error with the reason. Mirrors lidlCheckRecords' shape
// (lidl_compat.h) so main.cpp treats both the same way.
//
// It lives beside the consumer emitter because that is the only emitter left
// that can produce a caller for such a method; it was previously carried by the
// retired `--backend qt` provider emitter, which shared the constraint.
bool lidlCheckOptionalReturns(const ModuleDecl& module, QString* error);

// Whether the emitted wrapper targets ONE fixed module or binds at runtime.
//   Static — `<Class>(LogosAPI*)`, the module name baked into every call.
//            What a concrete `dependencies` entry generates.
//   Bound  — `<Class>(LogosAPI*, const QString& moduleName)`, the name held in
//            `m_moduleName`. What an `interface_dependencies` entry generates,
//            reached through the umbrella's `bind_<name>(...)`.
enum class QtConsumerBind { Static, Bound };

// How the emitted wrapper obtains its TRANSPORT binding. Deliberately a second
// enum rather than two more QtConsumerBind values: QtConsumerBind decides the
// call TARGET, this decides the call ORIGIN, and the four combinations are all
// meaningful. Folding them into one enum would make every switch answer two
// questions at once.
//
//   FromApi        — `<Class>(LogosAPI*[, target])`. The origin is DERIVED,
//                    inside LpBridge::forTarget, from `api->moduleName()`. What
//                    every Qt plugin module generates today, and the default so
//                    that existing callers and their output are unchanged.
//   ExplicitOrigin — `<Class>(const QString& origin[, const QString& target])`.
//                    No LogosAPI anywhere in the class: the wrapper binds
//                    through LpBridge::forOrigin with the origin it was HANDED.
//                    This is what lets a module with no identity object — a
//                    cdylib, whose provider surface is the std
//                    `logos_module_impl.h` C ABI — still hold Qt-typed
//                    dependency wrappers.
//
// The distinction is a security boundary, not a convenience. An origin derived
// from someone else's LogosAPI is that module's identity, and calls made under
// it carry that module's capabilities. The ExplicitOrigin wrapper therefore
// takes its origin as a constructor argument and passes it through verbatim; it
// has no way to infer one, by construction.
enum class QtConsumerBinding { FromApi, ExplicitOrigin };

// `moduleName` is the DEPENDENCY/INTERFACE name the umbrella uses (not
// necessarily `module.name` from the contract): it names the call target in
// Static mode and only the files/class in Bound mode. `className` is the
// wrapper class.
//
// `binding` is trailing and defaulted so the LogosAPI-taking output — the one
// every current consumer compiles against — is byte-for-byte what it was.
QString lidlMakeQtConsumerHeader(const ModuleDecl& module,
                                 const QString& moduleName,
                                 const QString& className,
                                 QtConsumerBind bind,
                                 QtConsumerBinding binding = QtConsumerBinding::FromApi);

QString lidlMakeQtConsumerSource(const ModuleDecl& module,
                                 const QString& moduleName,
                                 const QString& className,
                                 const QString& headerBaseName,
                                 QtConsumerBind bind,
                                 QtConsumerBinding binding = QtConsumerBinding::FromApi);

#endif  // LIDL_GEN_QT_CONSUMER_H
