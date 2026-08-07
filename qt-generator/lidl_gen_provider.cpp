#include "lidl_gen_provider.h"
#include "lidl_emit_common.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>

// ---------------------------------------------------------------------------
// Type mapping: LIDL → C++ std types
// ---------------------------------------------------------------------------



// ---------------------------------------------------------------------------
// Optionality
// ---------------------------------------------------------------------------

// A usable `?T`. A degenerate Optional carrying no value type (unreachable from
// the parser, constructible by hand or over the JSON bridge) keeps the untyped
// fallback rather than pretending to be optional — the same guard the cdylib
// backend and the Rust backend apply.
static bool isOptionalSlot(const TypeExpr& te)
{
    return te.kind == TypeExpr::Optional && !te.elements.empty();
}

static bool methodHasOptionalParam(const MethodDecl& md)
{
    for (const ParamDecl& p : md.params)
        if (isOptionalSlot(p.type)) return true;
    return false;
}

static bool moduleHasOptionalParam(const ModuleDecl& module)
{
    for (const MethodDecl& md : module.methods)
        if (methodHasOptionalParam(md)) return true;
    for (const EventDecl& ev : module.events)
        for (const ParamDecl& p : ev.params)
            if (isOptionalSlot(p.type)) return true;
    return false;
}

// The number of leading argument slots a caller MUST supply: one past the last
// required parameter. A trailing `?T` lowers it; an optional in the middle does
// not, because the slot after it is positional and still has to be reachable.
static int minRequiredArgs(const MethodDecl& md)
{
    int minArgs = 0;
    for (int i = 0; i < md.params.size(); ++i)
        if (!isOptionalSlot(md.params[i].type)) minArgs = i + 1;
    return minArgs;
}

bool lidlCheckOptionalReturns(const ModuleDecl& module, QString* error)
{
    for (const MethodDecl& md : module.methods) {
        if (md.returnType.kind != TypeExpr::Optional) continue;
        if (error) {
            *error = QStringLiteral(
                "%1: an optional RETURN (`-> ?T`) is not supported. An empty `?T` is "
                "spelled JSON null on the wire, and null is already how this path "
                "reports a FAILED call (logos_json_convert maps it to an invalid "
                "QVariant, which core_service reports as METHOD_FAILED) — so \"found "
                "nothing\" would be indistinguishable from \"the call failed\" for every "
                "non-Rust caller. Take `?T` as a PARAMETER, or return a `result`.")
                .arg(qs(md.name));
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Conversion helpers: Qt type ↔ std type
// ---------------------------------------------------------------------------

static QString qtParamToStd(const TypeExpr& te, const QString& paramName)
{
    // `?T` reaches the author as std::optional<T>. The Qt surface is an untyped
    // QVariant (lidlTypeToQt is documented to lose the value type there), so the
    // value is decoded through the CANONICAL codec rather than a parallel one:
    // an absent or null slot becomes an empty optional, and anything else is
    // decoded as T by the SAME decoder a required T would get. Optionality
    // widens the domain; it does not switch type checking off.
    if (isOptionalSlot(te)) {
        return "logos::fromJson<" + lidlTypeToStd(te) + ">(logos::qvariantToNlohmann("
             + paramName + "), \"" + paramName + "\")";
    }

    if (!lidlIsStdConvertible(te))
        return paramName;

    if (te.kind == TypeExpr::Primitive) {
        if (te.name == "tstr")    return paramName + ".toStdString()";
        if (te.name == "bstr")    return "std::vector<uint8_t>(" + paramName + ".begin(), " + paramName + ".end())";
        if (te.name == "int")     return "static_cast<int64_t>(" + paramName + ")";
        if (te.name == "uint")    return "static_cast<uint64_t>(" + paramName + ")";
        return paramName;
    }
    if (te.kind == TypeExpr::Array && te.elements.size() == 1) {
        const TypeExpr& elem = te.elements[0];
        if (elem.kind == TypeExpr::Primitive && elem.name == "tstr")
            return "lidlToStdStringVector(" + paramName + ")";
        return "lidlToStdVector_" + qs(elem.name) + "(" + paramName + ")";
    }
    return paramName;
}

static QString stdReturnToQt(const TypeExpr& te, const QString& varName)
{
    if (!lidlIsStdConvertible(te))
        return varName;

    if (te.kind == TypeExpr::Primitive) {
        if (te.name == "tstr")    return "QString::fromStdString(" + varName + ")";
        if (te.name == "bstr")    return "QByteArray(reinterpret_cast<const char*>(" + varName + ".data()), static_cast<int>(" + varName + ".size()))";
        // 64-bit, not int: `int` is int64_t and `uint` is uint64_t on the std
        // side, so casting to a 32-bit int silently truncated every value past
        // 2^31 on the way back out to Qt — the same class of defect as the
        // parse_u64-as-u128 wrap, and invisible until a large id or a wei
        // amount crossed it.
        if (te.name == "int")     return "static_cast<qlonglong>(" + varName + ")";
        if (te.name == "uint")    return "static_cast<qulonglong>(" + varName + ")";
        return varName;
    }
    if (te.kind == TypeExpr::Array && te.elements.size() == 1) {
        const TypeExpr& elem = te.elements[0];
        if (elem.kind == TypeExpr::Primitive && elem.name == "tstr")
            return "lidlToQStringList(" + varName + ")";
        return "lidlToQVariantList_" + qs(elem.name) + "(" + varName + ")";
    }
    return varName;
}

// The std spelling of a LIDL primitive, for checking the ELEMENTS of an array
// whose Qt spelling (QVariantList) has erased them. Empty = no rule.
static QString lidlPrimitiveStdType(const TypeExpr& te)
{
    if (te.kind != TypeExpr::Primitive) return QString();
    if (te.name == "tstr")    return "std::string";
    if (te.name == "bstr")    return "std::vector<uint8_t>";
    if (te.name == "int")     return "int64_t";
    if (te.name == "uint")    return "uint64_t";
    if (te.name == "float64") return "double";
    if (te.name == "bool")    return "bool";
    return QString();
}

// Incoming argument -> the declared type.
//
// This used to be the Qt coercions (`.toInt()`, `.toBool()`, `.toList()`),
// which is the same defect the header-scanning generator and the QMetaObject
// dispatch had: echoUint(-1) reached the author's body as 18446744073709551615
// and echoMap(5) as {}, where every non-Qt provider answers
// {"code":"dispatch_failed"}. The rule is the canonical codec's — see
// logos-protocol/cpp/logos_qt_arg_decode.h.
//
// This backend is not wired into module-builder's autoCodegen yet; it is fixed
// with the other two so the bug is not waiting for whoever wires it up. Being
// LIDL-driven it can do strictly MORE than they can: a `[uint]` parameter is
// just QVariantList in a C++ signature, but the declaration still names the
// element type, so the elements are checked here.
static QString variantToQtArg(const TypeExpr& te, int argIdx)
{
    // `.value(i)`, never `.at(i)`: `at` on a QList is unchecked, so a caller
    // sending fewer arguments than the contract declares was reading past the
    // end. `value` returns a default-constructed (invalid) QVariant instead,
    // which the codec below rejects with a named path — and which an OPTIONAL
    // slot reads as its empty inhabitant. The arity gate in callMethod catches
    // the required case first with a better message; this is the backstop that
    // makes an out-of-range slot impossible rather than merely unlikely.
    const QString a = "args.value(" + QString::number(argIdx) + ")";
    const QString path = "arg" + QString::number(argIdx);

    // `?T` is untyped on the Qt surface (QVariant), so the slot is handed over
    // verbatim and decoded once, in qtParamToStd, by the canonical codec.
    // Decoding it here as well would type-check the value twice and, worse,
    // reject an absent slot before the optional could absorb it.
    if (isOptionalSlot(te))
        return a;

    const QString qt = lidlTypeToQt(te);

    if (qt == "QVariantList" && te.kind == TypeExpr::Array && te.elements.size() == 1) {
        const QString elem = lidlPrimitiveStdType(te.elements[0]);
        if (!elem.isEmpty())
            return "logos::qtArgListOf<" + elem + ">(" + a + ", \"" + path + "\")";
    }

    static const QSet<QString> codecKnown = {
        "bool", "int", "qlonglong", "qulonglong", "double", "float",
        "QString", "QStringList", "QByteArray", "QJsonArray", "QJsonObject",
        "QVariantList", "QVariantMap", "QVariant", "LogosResult"
    };
    if (!codecKnown.contains(qt)) return a;
    return "logos::qtArgFromVariant<" + qt + ">(" + a + ", \"" + path + "\")";
}

// ---------------------------------------------------------------------------
// Provider header generation
// ---------------------------------------------------------------------------

QString lidlMakeProviderHeader(const ModuleDecl& module,
                               const QString& implClass,
                               const QString& implHeader)
{
    QString className = lidlToPascalCase(qs(module.name));
    QString providerObjectClass = className + "ProviderObject";
    QString pluginClass = className + "Plugin";
    QString h;
    QTextStream s(&h);

    s << "// AUTO-GENERATED by logos-cpp-generator -- do not edit\n";
    s << "#pragma once\n\n";

    s << "#include <QObject>\n";
    s << "#include <QString>\n";
    s << "#include <QVariant>\n";
    s << "#include <QStringList>\n";
    s << "#include <QVariantList>\n";
    s << "#include <QVariantMap>\n";
    s << "#include <QByteArray>\n";
    s << "#include <QJsonArray>\n";
    s << "#include <string>\n";
    s << "#include <vector>\n";
    s << "#include <cstdint>\n";
    s << "#include <functional>\n\n";

    s << "#include \"logos_provider_object.h\"\n";
    s << "#include \"interface.h\"\n";
    s << "#include \"logos_types.h\"\n";
    // The generated provider always emits an `onInit(LogosAPI*) override`
    // that — when the impl class inherits from LogosModuleContext —
    // copies the three runtime-injected properties (modulePath,
    // instanceId, instancePersistencePath) into the context base AND
    // builds a per-module `LogosModules` aggregate so impls can call
    // other modules without ever touching the raw `LogosAPI`. Both
    // dispatch paths sit behind SFINAE'd helpers in
    // logos_module_context.h, so modules that don't inherit
    // LogosModuleContext compile unchanged (the helpers resolve to
    // no-op overloads).
    s << "#include \"logos_api.h\"\n";
    s << "#include \"logos_module_context.h\"\n";
    // logos_sdk.h is generated alongside this header by the codegen's
    // umbrella pass (lidl_gen_client.cpp / legacy/main.cpp) and defines
    // the per-module `LogosModules` struct from the module's
    // metadata.json#dependencies list. Always present in the build's
    // generated_code/ directory; included unconditionally so the
    // onInit override below can construct an instance.
    s << "#include \"logos_sdk.h\"\n";
    s << "#include <memory>\n";
    s << "#include <type_traits>\n\n";

    s << "#include \"" << implHeader << "\"\n\n";

    // Conversion helpers (only if needed)
    bool needsStringVecHelper = false;
    for (const MethodDecl& md : module.methods) {
        for (const ParamDecl& pd : md.params) {
            if (pd.type.kind == TypeExpr::Array && pd.type.elements.size() == 1
                && pd.type.elements[0].kind == TypeExpr::Primitive
                && pd.type.elements[0].name == "tstr")
                needsStringVecHelper = true;
        }
        if (md.returnType.kind == TypeExpr::Array && md.returnType.elements.size() == 1
            && md.returnType.elements[0].kind == TypeExpr::Primitive
            && md.returnType.elements[0].name == "tstr")
            needsStringVecHelper = true;
    }

    if (needsStringVecHelper) {
        s << "namespace {\n";
        s << "inline QStringList lidlToQStringList(const std::vector<std::string>& v) {\n";
        s << "    QStringList result;\n";
        s << "    result.reserve(static_cast<int>(v.size()));\n";
        s << "    for (const auto& s : v)\n";
        s << "        result.append(QString::fromStdString(s));\n";
        s << "    return result;\n";
        s << "}\n\n";
        s << "inline std::vector<std::string> lidlToStdStringVector(const QStringList& list) {\n";
        s << "    std::vector<std::string> result;\n";
        s << "    result.reserve(static_cast<size_t>(list.size()));\n";
        s << "    for (const auto& s : list)\n";
        s << "        result.push_back(s.toStdString());\n";
        s << "    return result;\n";
        s << "}\n";
        s << "} // anonymous namespace\n\n";
    }

    // `?T` decodes through the canonical codec, which needs the codec itself and
    // the QVariant->json bridge. Emitted only when the contract actually has an
    // optional slot, so a contract without one keeps byte-identical output.
    if (moduleHasOptionalParam(module)) {
        s << "#include <optional>\n";
        s << "#include \"logos_codec.h\"\n";
        s << "#include \"logos_json_convert.h\"\n\n";
    }

    // Emit the nlohmannToQVariant helper only for a `result` return.
    //
    // It used to be emitted for the jsonReturn shapes too, back when they were
    // (wrongly) routed through it — on this backend the impl hands those back as
    // Qt types already, so they never needed a json conversion.
    bool needsNlohmannHelper = false;
    bool needsResultHelper = false;
    for (const MethodDecl& md : module.methods) {
        // NOT jsonReturn: those shapes come back from the impl as Qt types
        // already and no longer touch the helper. `result` is the one that
        // genuinely does — StdLogosResult::value IS an nlohmann::json, and
        // stdResultToQt converts it.
        if (md.resultReturn) { needsNlohmannHelper = true; needsResultHelper = true; }
    }
    if (needsNlohmannHelper) {
        s << "#include <nlohmann/json.hpp>\n\n";
        s << "namespace {\n";
        s << "inline QVariant nlohmannToQVariant(const nlohmann::json& j) {\n";
        s << "    if (j.is_null())    return QVariant();\n";
        s << "    if (j.is_boolean()) return QVariant(j.get<bool>());\n";
        s << "    if (j.is_number_integer()) return QVariant(static_cast<qlonglong>(j.get<int64_t>()));\n";
        s << "    if (j.is_number_unsigned()) return QVariant(static_cast<qulonglong>(j.get<uint64_t>()));\n";
        s << "    if (j.is_number_float()) return QVariant(j.get<double>());\n";
        s << "    if (j.is_string()) return QVariant(QString::fromStdString(j.get<std::string>()));\n";
        s << "    if (j.is_array()) {\n";
        s << "        QVariantList list;\n";
        s << "        list.reserve(static_cast<int>(j.size()));\n";
        s << "        for (const auto& elem : j)\n";
        s << "            list.append(nlohmannToQVariant(elem));\n";
        s << "        return QVariant(list);\n";
        s << "    }\n";
        s << "    if (j.is_object()) {\n";
        s << "        QVariantMap map;\n";
        s << "        for (auto it = j.begin(); it != j.end(); ++it)\n";
        s << "            map.insert(QString::fromStdString(it.key()), nlohmannToQVariant(it.value()));\n";
        s << "        return QVariant(map);\n";
        s << "    }\n";
        s << "    return QVariant();\n";
        s << "}\n";
        if (needsResultHelper) {
            s << "\n";
            s << "#include \"logos_result.h\"\n";
            s << "inline LogosResult stdResultToQt(const StdLogosResult& r) {\n";
            s << "    LogosResult qr;\n";
            s << "    qr.success = r.success;\n";
            s << "    qr.value = nlohmannToQVariant(r.value);\n";
            s << "    qr.error = r.error.empty() ? QVariant() : QVariant(QString::fromStdString(r.error));\n";
            s << "    return qr;\n";
            s << "}\n";
        }
        s << "} // anonymous namespace\n\n";
    }

    // --- ProviderObject class ---
    s << "class " << providerObjectClass << " : public LogosProviderBase {\n";
    s << "    LOGOS_PROVIDER(" << providerObjectClass << ", \""
      << module.name << "\", \"" << (module.version.empty() ? "0.0.0" : module.version) << "\")\n\n";
    s << "public:\n";

    for (const MethodDecl& md : module.methods) {
        QString qtRet = lidlTypeToQt(md.returnType);
        bool retConvertible = lidlIsStdConvertible(md.returnType);

        s << "    " << qtRet << " " << md.name << "(";
        for (int i = 0; i < md.params.size(); ++i) {
            QString qt = lidlTypeToQt(md.params[i].type);
            if (qt == "QString" || qt == "QByteArray" || qt == "QStringList"
                || qt == "QVariantList" || qt == "QVariantMap" || qt == "LogosResult")
                s << "const " << qt << "& " << md.params[i].name;
            else
                s << qt << " " << md.params[i].name;
            if (i + 1 < md.params.size()) s << ", ";
        }
        s << ") {\n";

        if (qtRet == "void") {
            s << "        m_impl." << md.name << "(";
            for (int i = 0; i < md.params.size(); ++i) {
                s << qtParamToStd(md.params[i].type, qs(md.params[i].name));
                if (i + 1 < md.params.size()) s << ", ";
            }
            s << ");\n";
        } else if (md.jsonReturn) {
            // `any`, `{tstr: T}` and `[any]` — the three shapes the LIDL parser
            // marks jsonReturn.
            //
            // The impl returns these ALREADY AS QT TYPES: lidlTypeToStd maps
            // `any` to QVariant, a map to QVariantMap and `[any]` to
            // QVariantList. So there is nothing to convert, and the conversion
            // that used to be here was wrong twice over — it fed a Qt value to
            // nlohmannToQVariant, which takes an nlohmann::json, and it then
            // forced everything that was not a map through `.toList()`, which
            // would coerce an object or a scalar `any` into a list.
            //
            // (The name is inherited from the cdylib path, where an impl really
            // does hand back LogosMap/LogosList. This backend's impls never do.)
            s << "        return m_impl." << md.name << "(";
            for (int i = 0; i < md.params.size(); ++i) {
                s << qtParamToStd(md.params[i].type, qs(md.params[i].name));
                if (i + 1 < md.params.size()) s << ", ";
            }
            s << ");\n";
        } else if (md.resultReturn) {
            // StdLogosResult: impl returns pure-C++ result, convert to Qt LogosResult
            s << "        auto _result = m_impl." << md.name << "(";
            for (int i = 0; i < md.params.size(); ++i) {
                s << qtParamToStd(md.params[i].type, qs(md.params[i].name));
                if (i + 1 < md.params.size()) s << ", ";
            }
            s << ");\n";
            s << "        return stdResultToQt(_result);\n";
        } else if (retConvertible) {
            s << "        auto _result = m_impl." << md.name << "(";
            for (int i = 0; i < md.params.size(); ++i) {
                s << qtParamToStd(md.params[i].type, qs(md.params[i].name));
                if (i + 1 < md.params.size()) s << ", ";
            }
            s << ");\n";
            s << "        return " << stdReturnToQt(md.returnType, "_result") << ";\n";
        } else {
            s << "        return m_impl." << md.name << "(";
            for (int i = 0; i < md.params.size(); ++i) {
                s << qtParamToStd(md.params[i].type, qs(md.params[i].name));
                if (i + 1 < md.params.size()) s << ", ";
            }
            s << ");\n";
        }

        s << "    }\n\n";
    }

    // Always emit an `onInit` override that:
    //   1. copies the three runtime-injected LogosAPI properties
    //      (modulePath, instanceId, instancePersistencePath) into the
    //      impl when it opts in by inheriting LogosModuleContext;
    //   2. builds a `LogosModules` aggregate (auto-generated by the
    //      codegen's umbrella pass — `generated_code/logos_sdk.h`)
    //      from the LogosAPI and threads its pointer into the same
    //      context base, giving the impl typed access to its
    //      declared dependencies via `modules().<dep>...`.
    //
    // Both wire-ups go through SFINAE'd helpers
    // (`_logos_codegen_::maybeSet*`) so impls that don't inherit
    // LogosModuleContext compile unchanged and pay zero runtime cost
    // (the no-op overloads inline away). The full LogosAPI is never
    // exposed to user code.
    //
    // `m_logosModules` is owned by the provider object, lifetime
    // matched to it — the impl is a member of this same provider
    // (`m_impl`), so the pointer the context holds stays valid for
    // the impl's entire lifetime.
    s << "protected:\n";
    s << "    void onInit(LogosAPI* api) override {\n";
    s << "        if (!api) return;\n";
    // Order matters: the modules() aggregate and the event wiring must be
    // in place BEFORE maybeSetContext — setting the context fires the
    // impl's onContextReady() hook, whose documented contract is "do your
    // one-time setup here", which includes typed dependency calls
    // (modules().dep.method()) and typed event subscriptions/emission.
    s << "        m_logosModules = std::make_unique<LogosModules>(api);\n";
    s << "        _logos_codegen_::maybeSetLogosModules(m_impl, m_logosModules.get());\n";
    // Wire the impl's `logos_events:` declarations to LogosProviderBase's
    // emitEvent. Codegen-emitted `<name>_events.cpp` bodies call
    // `this->emitEventImpl_(name, &args)`; the lambda below casts the
    // void* back to QVariantList and routes through the existing wire.
    s << "        _logos_codegen_::maybeSetEmitEvent(m_impl,\n";
    s << "            [this](const std::string& name, void* args) {\n";
    s << "                emitEvent(QString::fromStdString(name),\n";
    s << "                          *static_cast<QVariantList*>(args));\n";
    s << "            });\n";
    // Context LAST: _logosCoreSetContext_ fires onContextReady() — by then
    // the impl must be fully wired (see ordering note above).
    s << "        _logos_codegen_::maybeSetContext(m_impl,\n";
    s << "            api->property(\"modulePath\").toString().toStdString(),\n";
    s << "            api->property(\"instanceId\").toString().toStdString(),\n";
    s << "            api->property(\"instancePersistencePath\").toString().toStdString());\n";
    s << "    }\n\n";

    if (!module.events.empty()) {
        s << "protected:\n";
        for (const EventDecl& ed : module.events) {
            QString methodName = "emit" + lidlToPascalCase(qs(ed.name));
            s << "    void " << methodName << "(";
            for (int i = 0; i < ed.params.size(); ++i) {
                QString qt = lidlTypeToQt(ed.params[i].type);
                if (qt == "QString" || qt == "QByteArray" || qt == "QStringList"
                    || qt == "QVariantList" || qt == "QVariantMap" || qt == "LogosResult")
                    s << "const " << qt << "& " << ed.params[i].name;
                else
                    s << qt << " " << ed.params[i].name;
                if (i + 1 < ed.params.size()) s << ", ";
            }
            s << ") {\n";
            s << "        emitEvent(\"" << ed.name << "\", QVariantList{";
            for (int i = 0; i < ed.params.size(); ++i) {
                s << "QVariant::fromValue(" << ed.params[i].name << ")";
                if (i + 1 < ed.params.size()) s << ", ";
            }
            s << "});\n";
            s << "    }\n\n";
        }
    }

    s << "private:\n";
    // Built by onInit; lives for the provider's full lifetime. The impl
    // (declared next, so destroyed first — reverse-of-construction
    // order) holds only a non-owning pointer to it via the
    // LogosModuleContext base. Using unique_ptr instead of a direct
    // member so the build doesn't require LogosModules to be
    // default-constructible (it isn't — it takes a LogosAPI*).
    s << "    std::unique_ptr<LogosModules> m_logosModules;\n";
    s << "    " << implClass << " m_impl;\n";
    s << "};\n\n";

    // --- Plugin/Loader class ---
    s << "class " << pluginClass << " : public QObject, public PluginInterface, public LogosProviderPlugin {\n";
    s << "    Q_OBJECT\n";
    s << "    Q_PLUGIN_METADATA(IID LogosProviderPlugin_iid FILE \"metadata.json\")\n";
    s << "    Q_INTERFACES(PluginInterface LogosProviderPlugin)\n\n";
    s << "public:\n";
    s << "    QString name() const override { return QStringLiteral(\"" << module.name << "\"); }\n";
    s << "    QString version() const override { return QStringLiteral(\""
      << (module.version.empty() ? "0.0.0" : module.version) << "\"); }\n";
    s << "    LogosProviderObject* createProviderObject() override {\n";
    s << "        return new " << providerObjectClass << "();\n";
    s << "    }\n";
    s << "};\n";

    return h;
}

// ---------------------------------------------------------------------------
// Dispatch source generation
// ---------------------------------------------------------------------------

QString lidlMakeProviderDispatch(const ModuleDecl& module)
{
    QString className = lidlToPascalCase(qs(module.name));
    QString providerObjectClass = className + "ProviderObject";
    QString c;
    QTextStream s(&c);

    s << "// AUTO-GENERATED by logos-cpp-generator -- do not edit\n";
    s << "#include \"" << module.name << "_qt_glue.h\"\n";
    s << "#include <QJsonArray>\n";
    s << "#include <QJsonObject>\n";
    s << "#include <QVariant>\n";
    s << "#include <QString>\n";
    s << "#include \"logos_types.h\"\n";
    s << "#include \"logos_qt_arg_decode.h\"\n";
    s << "#include <exception>\n\n";

    // --- callMethod ---
    // The dispatch body is wrapped in a catch-all: any exception the author's
    // code lets escape becomes an ordinary method failure (invalid QVariant)
    // instead of unwinding through Qt event dispatch and killing the module
    // process.
    s << "QVariant " << providerObjectClass
      << "::callMethod(const QString& methodName, const QVariantList& args)\n{\n";
    s << "    try {\n";

    for (const MethodDecl& md : module.methods) {
        QString qtRet = lidlTypeToQt(md.returnType);
        s << "    if (methodName == \"" << md.name << "\") {\n";

        // Too few arguments is a REJECTED call, named and counted, rather than
        // an out-of-range read. A trailing `?T` lowers this bound, which is the
        // whole point of an optional slot: the caller may simply stop early.
        const int minArgs = minRequiredArgs(md);
        if (minArgs > 0) {
            s << "        if (args.size() < " << minArgs << ")\n";
            s << "            return logos::dispatchFailedVariant(providerName(),\n";
            s << "                QStringLiteral(\"" << md.name << ": expected at least "
              << minArgs << " argument(s), got \") + QString::number(args.size()));\n";
        }

        if (qtRet == "void") {
            s << "        " << md.name << "(";
            for (int i = 0; i < md.params.size(); ++i) {
                s << variantToQtArg(md.params[i].type, i);
                if (i + 1 < md.params.size()) s << ", ";
            }
            s << ");\n";
            s << "        return QVariant(true);\n";
        } else {
            s << "        return QVariant::fromValue(" << md.name << "(";
            for (int i = 0; i < md.params.size(); ++i) {
                s << variantToQtArg(md.params[i].type, i);
                if (i + 1 < md.params.size()) s << ", ";
            }
            s << "));\n";
        }
        s << "    }\n";
    }

    // An argument the declared type cannot represent is a REJECTED call, not a
    // failed one — the canonical {"code":"dispatch_failed", ...} object, the
    // same answer the cdylib dispatch and the Rust provider give.
    s << "    } catch (const logos::CodecError& e) {\n";
    s << "        qWarning() << \"" << providerObjectClass
      << "::callMethod:\" << methodName << \"rejected:\" << e.what();\n";
    s << "        return logos::dispatchFailedVariant(providerName(), "
         "QString::fromUtf8(e.what()));\n";
    s << "    } catch (const std::exception& e) {\n";
    s << "        qWarning() << \"" << providerObjectClass
      << "::callMethod:\" << methodName << \"failed:\" << e.what();\n";
    s << "        return QVariant();\n";
    s << "    }\n";
    s << "    qWarning() << \"" << providerObjectClass
      << "::callMethod: unknown method:\" << methodName;\n";
    s << "    return QVariant();\n";
    s << "}\n\n";

    // --- getMethods ---
    s << "QJsonArray " << providerObjectClass << "::getMethods()\n{\n";
    s << "    QJsonArray methods;\n";

    for (const MethodDecl& md : module.methods) {
        QString qtRet = lidlTypeToQt(md.returnType);
        s << "    {\n";
        s << "        QJsonObject obj;\n";
        s << "        obj[\"type\"] = QStringLiteral(\"method\");\n";
        s << "        obj[\"name\"] = QStringLiteral(\"" << md.name << "\");\n";
        s << "        obj[\"returnType\"] = QStringLiteral(\"" << qtRet << "\");\n";
        s << "        obj[\"isInvokable\"] = true;\n";
        if (!md.description.empty()) {
            QString escDesc = qs(md.description);
            escDesc.replace('\\', "\\\\");
            escDesc.replace('"', "\\\"");
            escDesc.replace('\n', "\\n");
            s << "        obj[\"description\"] = QStringLiteral(\"" << escDesc << "\");\n";
        }

        QString sig = qs(md.name) + "(";
        for (int i = 0; i < md.params.size(); ++i) {
            sig += lidlTypeToQt(md.params[i].type);
            if (i + 1 < md.params.size()) sig += ",";
        }
        sig += ")";
        s << "        obj[\"signature\"] = QStringLiteral(\"" << sig << "\");\n";

        if (!md.params.empty()) {
            s << "        QJsonArray params;\n";
            for (int i = 0; i < md.params.size(); ++i) {
                // `optional` is ADDITIVE and emitted only when true, so an
                // interface with no optionals is byte-identical to what this
                // generator produced before. A reader that does not know the key
                // sees exactly today's object.
                const QString opt = isOptionalSlot(md.params[i].type)
                                        ? QStringLiteral(", {\"optional\", true}")
                                        : QString();
                s << "        params.append(QJsonObject{{\"type\", QStringLiteral(\""
                  << lidlTypeToQt(md.params[i].type) << "\")}, {\"name\", QStringLiteral(\""
                  << md.params[i].name << "\")}" << opt << "});\n";
            }
            s << "        obj[\"parameters\"] = params;\n";
        }

        s << "        methods.append(obj);\n";
        s << "    }\n";
    }

    // Events are appended to the SAME interface list, tagged type "event" (and
    // with no returnType/isInvokable — they are void/fire-and-forget). Folding
    // them into getMethods() instead of adding a getEvents() vtable slot keeps
    // LogosProviderObject's vtable layout stable, so old/new hosts and modules
    // stay binary-compatible. Callers split the list back out by "type" (see
    // ModuleProxy::getPluginMethods/getPluginEvents/getPluginInterface).
    for (const EventDecl& ed : module.events) {
        s << "    {\n";
        s << "        QJsonObject obj;\n";
        s << "        obj[\"type\"] = QStringLiteral(\"event\");\n";
        s << "        obj[\"name\"] = QStringLiteral(\"" << ed.name << "\");\n";
        if (!ed.description.empty()) {
            QString escDesc = qs(ed.description);
            escDesc.replace('\\', "\\\\");
            escDesc.replace('"', "\\\"");
            escDesc.replace('\n', "\\n");
            s << "        obj[\"description\"] = QStringLiteral(\"" << escDesc << "\");\n";
        }

        QString sig = qs(ed.name) + "(";
        for (int i = 0; i < ed.params.size(); ++i) {
            sig += lidlTypeToQt(ed.params[i].type);
            if (i + 1 < ed.params.size()) sig += ",";
        }
        sig += ")";
        s << "        obj[\"signature\"] = QStringLiteral(\"" << sig << "\");\n";

        if (!ed.params.empty()) {
            s << "        QJsonArray params;\n";
            for (int i = 0; i < ed.params.size(); ++i) {
                s << "        params.append(QJsonObject{{\"type\", QStringLiteral(\""
                  << lidlTypeToQt(ed.params[i].type) << "\")}, {\"name\", QStringLiteral(\""
                  << ed.params[i].name << "\")}});\n";
            }
            s << "        obj[\"parameters\"] = params;\n";
        }

        s << "        methods.append(obj);\n";
        s << "    }\n";
    }

    s << "    return methods;\n";
    s << "}\n";

    return c;
}

// ---------------------------------------------------------------------------
// Events source generation — Qt-MOC-style bodies for `logos_events:` decls
// ---------------------------------------------------------------------------
//
// The impl header declares typed event prototypes in a `logos_events:`
// section. The compiler sees them as ordinary public-method declarations
// (the macro expands to `public:`). This generator emits the matching
// definitions in a sidecar `<name>_events.cpp` — exactly the role that
// `moc_*.cpp` plays for Qt's `signals:`. Each body marshals typed args
// into a `QVariantList` and calls `LogosModuleContext::emitEventImpl_`,
// which the provider's onInit wired to LogosProviderBase::emitEvent (and
// onward over QRO).

// Returns a C++ expression of static type QVariant for the given
// std-typed parameter. Mirrors the type-mapping table in lidlTypeToStd.
static QString stdParamToQVariantExpr(const TypeExpr& te, const QString& pn)
{
    // `?T` — the author emits std::optional<T>, which is NOT a registered
    // metatype, so the QVariant::fromValue fallback at the bottom would put an
    // opaque blob on the wire. An empty optional is the invalid QVariant (the
    // same empty inhabitant the parameter direction reads back); a present one
    // marshals exactly as a required T would, so a subscriber cannot tell a
    // filled optional slot from a required one.
    if (isOptionalSlot(te)) {
        return "(" + pn + ".has_value() ? "
             + stdParamToQVariantExpr(optionalValueType(te), "(*" + pn + ")")
             + " : QVariant())";
    }

    if (te.kind == TypeExpr::Primitive) {
        if (te.name == "tstr")
            return "QVariant(QString::fromStdString(" + pn + "))";
        if (te.name == "bstr")
            return "QVariant(QByteArray(reinterpret_cast<const char*>(" + pn
                 + ".data()), static_cast<int>(" + pn + ".size())))";
        if (te.name == "int")
            return "QVariant(static_cast<qlonglong>(" + pn + "))";
        if (te.name == "uint")
            return "QVariant(static_cast<qulonglong>(" + pn + "))";
        if (te.name == "float64") return "QVariant(" + pn + ")";
        if (te.name == "bool")    return "QVariant(" + pn + ")";
    }
    if (te.kind == TypeExpr::Array && te.elements.size() == 1
        && te.elements[0].kind == TypeExpr::Primitive
        && te.elements[0].name == "tstr") {
        // std::vector<std::string> -> QStringList wrapped in QVariant
        return "QVariant([&](){ QStringList _l; _l.reserve(static_cast<int>(" + pn
             + ".size())); for (const auto& _e : " + pn
             + ") _l.append(QString::fromStdString(_e)); return _l; }())";
    }
    // Fallback — let QVariant::fromValue figure it out (works for primitives
    // and any QMetaType-registered user type).
    return "QVariant::fromValue(" + pn + ")";
}

QString lidlMakeEventsSource(const ModuleDecl& module,
                              const QString& implClass,
                              const QString& implHeader)
{
    QString c;
    QTextStream s(&c);
    s << "// AUTO-GENERATED by logos-cpp-generator -- do not edit\n";
    s << "//\n";
    s << "// Bodies for `logos_events:` methods declared in " << implHeader << ".\n";
    s << "// Each call marshals typed args into a QVariantList and routes\n";
    s << "// them through LogosModuleContext::emitEventImpl_, which the\n";
    s << "// generated provider wires to LogosProviderBase::emitEvent.\n";
    s << "#include \"" << implHeader << "\"\n";
    s << "#include <QString>\n";
    s << "#include <QByteArray>\n";
    s << "#include <QStringList>\n";
    s << "#include <QVariant>\n";
    s << "#include <QVariantList>\n";
    s << "#include <cstdint>\n";
    s << "#include <string>\n";
    s << "#include <vector>\n\n";

    for (const EventDecl& ed : module.events) {
        // Signature — mirrors the prototype the impl declared.
        s << "void " << implClass << "::" << ed.name << "(";
        for (int i = 0; i < ed.params.size(); ++i) {
            QString stdType = lidlTypeToStd(ed.params[i].type);
            const TypeExpr& te = ed.params[i].type;
            // Pass-by-const-ref for non-trivial std types; by-value for
            // primitives. Mirrors the existing method-signature shape.
            bool byRef = (te.kind == TypeExpr::Array)
                || (te.kind == TypeExpr::Primitive
                    && (te.name == "tstr" || te.name == "bstr"));
            if (byRef) s << "const " << stdType << "& " << ed.params[i].name;
            else       s << stdType << " " << ed.params[i].name;
            if (i + 1 < ed.params.size()) s << ", ";
        }
        s << ") {\n";
        s << "    QVariantList _args{";
        for (int i = 0; i < ed.params.size(); ++i) {
            s << stdParamToQVariantExpr(ed.params[i].type, qs(ed.params[i].name));
            if (i + 1 < ed.params.size()) s << ", ";
        }
        s << "};\n";
        s << "    this->emitEventImpl_(\"" << ed.name << "\", &_args);\n";
        s << "}\n\n";
    }

    return c;
}
