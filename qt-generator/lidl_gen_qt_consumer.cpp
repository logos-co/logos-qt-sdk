#include "lidl_gen_qt_consumer.h"

#include "lidl_emit_common.h"   // lidlTypeToQt / lidlToPascalCase — the ONE Qt type mapper

#include <QSet>
#include <QStringList>
#include <QTextStream>

// ---------------------------------------------------------------------------
// Emission for the Qt-typed consumer wrapper.
//
// Read this file with one question in mind: does it write a conversion? It must
// not. Every value crossing the boundary goes through exactly two spellings —
//
//     logos::qt::toWire(QVariant::fromValue(<expr>))     Qt   -> canonical JSON
//     logos::qt::fromWire<T>(<json>)                     JSON -> Qt
//
// — both one-line forwards to logos-protocol's canonical converters. The only
// per-type knowledge here is the SURFACE TYPE NAME (`lidlTypeToQt`) and the
// by-value/by-const-ref convention, i.e. the signature, not the conversion.
// Records are the single structural exception: a contract's `type Foo {...}` is
// a real C++ struct, so the generator emits a field-by-field encoder — whose
// every field still goes through those same two spellings.
// ---------------------------------------------------------------------------

namespace {

// ── records ─────────────────────────────────────────────────────────────────

bool isRecordName(const ModuleDecl& m, const std::string& n)
{
    for (const TypeDecl& t : m.types) if (t.name == n) return true;
    return false;
}

enum class RecShape { None, Scalar, List, Map };

// How a type mentions a record, if at all. Mirrors the shapes the Qt surface
// already exposed: `Foo`, `QList<Foo>`, `QMap<QString, Foo>`.
RecShape recordShape(const ModuleDecl& m, const TypeExpr& te, QString* elem)
{
    auto take = [&](const std::string& n) { if (elem) *elem = qs(n); };
    if (te.kind == TypeExpr::Named && isRecordName(m, te.name)) { take(te.name); return RecShape::Scalar; }
    if (te.kind == TypeExpr::Array && te.elements.size() == 1
        && te.elements[0].kind == TypeExpr::Named && isRecordName(m, te.elements[0].name)) {
        take(te.elements[0].name);
        return RecShape::List;
    }
    if (te.kind == TypeExpr::Map && te.elements.size() == 2
        && te.elements[1].kind == TypeExpr::Named && isRecordName(m, te.elements[1].name)) {
        take(te.elements[1].name);
        return RecShape::Map;
    }
    return RecShape::None;
}

QString recToWireFn(const QString& r)   { return "recToWire_" + r; }
QString recFromWireFn(const QString& r) { return "recFromWire_" + r; }

// ── surface types ───────────────────────────────────────────────────────────
//
// `qual` qualifies a nested record where class scope does not apply — a return
// type written before the `Class::` of a definition.
QString surfaceType(const ModuleDecl& m, const TypeExpr& te, const QString& qual, bool paramPosition)
{
    QString elem;
    switch (recordShape(m, te, &elem)) {
    case RecShape::Scalar: return qual + elem;
    case RecShape::List:   return "QList<" + qual + elem + ">";
    case RecShape::Map:    return "QMap<QString, " + qual + elem + ">";
    case RecShape::None:   break;
    }
    const QString t = lidlTypeToQt(te);
    // The one position-dependent spelling on the shipped surface: a `result`
    // RETURN is `LogosResult`, but a `result` PARAMETER has always been
    // `QVariant` (the legacy param table simply had no LogosResult entry).
    // Reproduced deliberately — this generator's contract is that no call site
    // moves, so an accidental "improvement" here would be a break.
    if (paramPosition && t == "LogosResult") return "QVariant";
    return t;
}

// By const-ref exactly where the shipped surface used it. QByteArray and
// LogosResult are by VALUE — that is what shipped, and widening it would change
// overload resolution and address-of at existing call sites. Records are
// structs and were always by const-ref.
bool byRef(const ModuleDecl& m, const TypeExpr& te, const QString& cppType)
{
    if (recordShape(m, te, nullptr) != RecShape::None) return true;
    return cppType == "QString" || cppType == "QStringList"
        || cppType == "QJsonArray" || cppType == "QVariantList" || cppType == "QVariantMap";
}

QString declParam(const ModuleDecl& m, const TypeExpr& te, const QString& name)
{
    const QString t = surfaceType(m, te, QString(), /*paramPosition=*/true);
    return byRef(m, te, t) ? "const " + t + "& " + name : t + " " + name;
}

// ── the two conversion spellings ────────────────────────────────────────────

QString toWire(const ModuleDecl& m, const TypeExpr& te, const QString& expr)
{
    QString elem;
    switch (recordShape(m, te, &elem)) {
    case RecShape::Scalar:
        return recToWireFn(elem) + "(" + expr + ")";
    case RecShape::List:
        return "[&]{ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : "
             + expr + ") __acc.push_back(" + recToWireFn(elem) + "(__e)); return __acc; }()";
    case RecShape::Map:
        return "[&]{ nlohmann::json __acc = nlohmann::json::object(); for (auto __i = " + expr
             + ".cbegin(); __i != " + expr + ".cend(); ++__i) __acc[__i.key().toStdString()] = "
             + recToWireFn(elem) + "(__i.value()); return __acc; }()";
    case RecShape::None:
        break;
    }
    // QVariant::fromValue makes the argument exactly ONE value — a bare
    // QVariantList-typed arg would otherwise be spread across the args array
    // (the historical "typed arrays arrive empty over the Qt path" bug). It is
    // a no-op for an already-QVariant (`any`) argument.
    return "logos::qt::toWire(QVariant::fromValue(" + expr + "))";
}

QString fromWire(const ModuleDecl& m, const TypeExpr& te, const QString& json, const QString& qual)
{
    QString elem;
    const QString cpp = surfaceType(m, te, qual, /*paramPosition=*/false);
    switch (recordShape(m, te, &elem)) {
    case RecShape::Scalar:
        return recFromWireFn(elem) + "(" + json + ")";
    case RecShape::List:
        return "[&]{ " + cpp + " __acc; const nlohmann::json& __src = " + json
             + "; if (__src.is_array()) for (const auto& __e : __src) __acc.push_back("
             + recFromWireFn(elem) + "(__e)); return __acc; }()";
    case RecShape::Map:
        return "[&]{ " + cpp + " __acc; const nlohmann::json& __src = " + json
             + "; if (__src.is_object()) for (auto __i = __src.begin(); __i != __src.end(); ++__i) "
               "__acc.insert(QString::fromStdString(__i.key()), " + recFromWireFn(elem)
             + "(__i.value())); return __acc; }()";
    case RecShape::None:
        break;
    }
    return "logos::qt::fromWire<" + cpp + ">(" + json + ")";
}

bool isVoid(const TypeExpr& te)
{
    return (te.kind == TypeExpr::Primitive || te.kind == TypeExpr::Named) && te.name == "void";
}

// `on` + the event name with its first letter capitalised. NOT PascalCase:
// `userLoggedIn` must stay `onUserLoggedIn`, not `onUserloggedin`.
QString eventAccessor(const std::string& evName)
{
    QString cap = qs(evName);
    if (!cap.isEmpty()) cap[0] = cap[0].toUpper();
    return "on" + cap;
}

// The callback parameter list of a typed event accessor.
QString eventCbParams(const ModuleDecl& m, const EventDecl& ev)
{
    QStringList parts;
    for (const ParamDecl& p : ev.params) parts << declParam(m, p.type, qs(p.name));
    return parts.join(", ");
}

}  // namespace

// ── header ──────────────────────────────────────────────────────────────────

QString lidlMakeQtConsumerHeader(const ModuleDecl& module,
                                 const QString& moduleName,
                                 const QString& className,
                                 QtConsumerBind bind)
{
    (void)moduleName;
    QString h;
    QTextStream s(&h);

    s << "#pragma once\n";
    // Deliberately the same include set the Qt consumer header always had: the
    // wrapper's own header must stay Qt-only, so a consumer's translation unit
    // does not start seeing nlohmann::json because of an implementation change.
    // The bridge (and json) is included by the .cpp.
    s << "#include <QString>\n";
    s << "#include <QVariant>\n";
    s << "#include <QStringList>\n";
    s << "#include <QJsonArray>\n";
    s << "#include <QVariantList>\n";
    s << "#include <QVariantMap>\n";
    s << "#include <functional>\n";
    s << "#include <utility>\n";
    s << "#include \"logos_types.h\"\n";
    s << "#include \"logos_api.h\"\n";
    s << "#include \"logos_api_client.h\"\n";
    s << "#include \"logos_call_error.h\"\n";
    s << "#include \"logos_object.h\"\n\n";
    // Forward declaration only — see the include comment above.
    s << "namespace logos { namespace qt { class LpBridge; } }\n\n";

    s << "class " << className << " {\n";
    s << "public:\n";

    if (!module.types.empty()) {
        s << "    // Record types declared by the contract.\n";
        for (const TypeDecl& t : module.types) {
            s << "    struct " << qs(t.name) << " {\n";
            for (const FieldDecl& f : t.fields)
                s << "        " << surfaceType(module, f.type, QString(), true)
                  << " " << qs(f.name) << "{};\n";
            s << "    };\n";
        }
        s << "\n";
    }

    if (bind == QtConsumerBind::Bound)
        s << "    explicit " << className << "(LogosAPI* api, const QString& moduleName);\n\n";
    else
        s << "    explicit " << className << "(LogosAPI* api);\n\n";

    s << "    using RawEventCallback = std::function<void(const QString&, const QVariantList&)>;\n";
    s << "    using EventCallback = std::function<void(const QVariantList&)>;\n\n";
    s << "    bool on(const QString& eventName, RawEventCallback callback);\n";
    s << "    bool on(const QString& eventName, EventCallback callback);\n";

    for (const EventDecl& ev : module.events) {
        if (ev.name.empty()) continue;
        s << "    bool " << eventAccessor(ev.name)
          << "(std::function<void(" << eventCbParams(module, ev) << ")> callback);\n";
    }
    if (!module.events.empty()) s << "\n";

    for (const MethodDecl& mtd : module.methods) {
        const QString ret = isVoid(mtd.returnType)
                                ? QStringLiteral("void")
                                : surfaceType(module, mtd.returnType, QString(), false);
        QStringList params;
        for (const ParamDecl& p : mtd.params) params << declParam(module, p.type, qs(p.name));

        QStringList sync = params;
        sync << "logos::CallError* err = nullptr";
        s << "    " << ret << " " << qs(mtd.name) << "(" << sync.join(", ") << ");\n";

        const QString cb = (ret == "void") ? QStringLiteral("std::function<void()>")
                                           : "std::function<void(" + ret + ")>";
        QStringList async = params;
        async << cb + " callback" << "Timeout timeout = Timeout()";
        s << "    void " << qs(mtd.name) << "Async(" << async.join(", ") << ");\n";
    }

    s << "\nprivate:\n";
    s << "    LogosAPI* m_api;\n";
    // Nothing CALLS m_client any more — the emit side (trigger /
    // setEventSource / eventSource) that was its only user is gone, because it
    // had no caller anywhere in the workspace. It is kept solely for the
    // side effect of its eager acquisition in the constructor:
    // `api->getClient(...)` is what mints this consumer's Qt-side capability
    // token, and token exchange is fire-once per process and never re-exchanged.
    // Dropping it would flip qtCap yes->no on top of the token delta this
    // veneer already carries, and a rejected call returns a bare QVariant() —
    // undetectable at the call site. Retire it deliberately, on its own, with a
    // soak; not as a side effect of deleting dead code.
    s << "    LogosAPIClient* m_client;\n";
    s << "    QString m_moduleName;\n";
    // Non-owning: the bridge is process-lifetime and keyed by (origin, target),
    // so this wrapper stays a cheap copyable handle and a subscription taken
    // out on a `bind_x(...)` TEMPORARY keeps delivering — the same lifetime the
    // LogosAPI-owned client used to provide.
    s << "    logos::qt::LpBridge* m_bridge;\n";
    s << "};\n";
    return h;
}

// ── source ──────────────────────────────────────────────────────────────────

QString lidlMakeQtConsumerSource(const ModuleDecl& module,
                                 const QString& moduleName,
                                 const QString& className,
                                 const QString& headerBaseName,
                                 QtConsumerBind bind)
{
    QString c;
    QTextStream s(&c);

    s << "#include \"" << headerBaseName << "\"\n\n";
    s << "#include <QDebug>\n";
    s << "#include <nlohmann/json.hpp>\n";
    s << "#include \"logos_qt_lp_bridge.h\"\n\n";

    const QString qual = className + "::";

    // Record <-> canonical JSON. Declared up front so records can reference
    // each other (and themselves, through a list field) in any order.
    if (!module.types.empty()) {
        for (const TypeDecl& t : module.types) {
            s << "static nlohmann::json " << recToWireFn(qs(t.name))
              << "(const " << qual << qs(t.name) << "& v);\n";
            s << "static " << qual << qs(t.name) << " " << recFromWireFn(qs(t.name))
              << "(const nlohmann::json& w);\n";
        }
        s << "\n";
        for (const TypeDecl& t : module.types) {
            s << "static nlohmann::json " << recToWireFn(qs(t.name))
              << "(const " << qual << qs(t.name) << "& v) {\n";
            s << "    nlohmann::json __j = nlohmann::json::object();\n";
            for (const FieldDecl& f : t.fields)
                s << "    __j[\"" << qs(f.name) << "\"] = "
                  << toWire(module, f.type, "v." + qs(f.name)) << ";\n";
            s << "    return __j;\n";
            s << "}\n\n";

            // A missing / mistyped field keeps its default rather than failing
            // the whole call — the same leniency the scalar paths have.
            s << "static " << qual << qs(t.name) << " " << recFromWireFn(qs(t.name))
              << "(const nlohmann::json& w) {\n";
            s << "    " << qual << qs(t.name) << " __out;\n";
            s << "    if (!w.is_object()) return __out;\n";
            for (const FieldDecl& f : t.fields)
                s << "    if (w.contains(\"" << qs(f.name) << "\")) __out." << qs(f.name)
                  << " = " << fromWire(module, f.type, "w.at(\"" + qs(f.name) + "\")", qual) << ";\n";
            s << "    return __out;\n";
            s << "}\n\n";
        }
    }

    // Constructor. `m_client` is still acquired eagerly (unchanged behaviour —
    // getClient is cached); `m_bridge` is the lp seam for everything else.
    if (bind == QtConsumerBind::Bound) {
        s << className << "::" << className << "(LogosAPI* api, const QString& moduleName)\n";
        s << "    : m_api(api), m_client(api->getClient(moduleName)), m_moduleName(moduleName),\n";
        s << "      m_bridge(logos::qt::LpBridge::forTarget(api, moduleName)) {}\n\n";
    } else {
        s << className << "::" << className << "(LogosAPI* api)\n";
        s << "    : m_api(api), m_client(api->getClient(\"" << moduleName << "\")),\n";
        s << "      m_moduleName(QStringLiteral(\"" << moduleName << "\")),\n";
        s << "      m_bridge(logos::qt::LpBridge::forTarget(api, QStringLiteral(\""
          << moduleName << "\"))) {}\n\n";
    }

    // Untyped subscription channel. Same signature, same "subscribe once,
    // delivered for the module's lifetime" contract; the payload is decoded by
    // the canonical args converter rather than by a table written here.
    s << "bool " << className << "::on(const QString& eventName, RawEventCallback callback) {\n";
    s << "    if (!callback) {\n";
    s << "        qWarning() << \"" << className << ": ignoring empty event callback for\" << eventName;\n";
    s << "        return false;\n";
    s << "    }\n";
    s << "    const QString _name = eventName;\n";
    s << "    return logos::qt::subscribe(m_bridge, eventName.toStdString(),\n";
    s << "        [callback, _name](nlohmann::json _a) {\n";
    s << "            callback(_name, logos::nlohmannArgsToQVariantList(_a));\n";
    s << "        });\n";
    s << "}\n\n";
    s << "bool " << className << "::on(const QString& eventName, EventCallback callback) {\n";
    s << "    if (!callback) {\n";
    s << "        qWarning() << \"" << className << ": ignoring empty event callback for\" << eventName;\n";
    s << "        return false;\n";
    s << "    }\n";
    s << "    return on(eventName, [callback](const QString&, const QVariantList& data) {\n";
    s << "        callback(data);\n";
    s << "    });\n";
    s << "}\n\n";

    // Typed event accessors.
    for (const EventDecl& ev : module.events) {
        if (ev.name.empty()) continue;
        s << "bool " << className << "::" << eventAccessor(ev.name)
          << "(std::function<void(" << eventCbParams(module, ev) << ")> callback) {\n";
        s << "    if (!callback) {\n";
        s << "        qWarning() << \"" << className << ": ignoring empty event callback for\" "
          << "<< QStringLiteral(\"" << qs(ev.name) << "\");\n";
        s << "        return false;\n";
        s << "    }\n";
        s << "    return logos::qt::subscribe(m_bridge, \"" << qs(ev.name)
          << "\", [callback](nlohmann::json _a) {\n";
        s << "        if (!_a.is_array() || _a.size() < " << ev.params.size() << ") return;\n";
        s << "        callback(";
        QStringList args;
        for (size_t i = 0; i < ev.params.size(); ++i)
            args << fromWire(module, ev.params[i].type, QString("_a.at(%1)").arg(i), qual);
        s << args.join(", ");
        s << ");\n";
        s << "    });\n";
        s << "}\n\n";
    }

    // Methods — sync and async share the ARGUMENT encoding and the RETURN
    // decoding, expression for expression. That is the point: the two used to
    // be separate tables that converted the same type differently, and there is
    // now nothing left for them to disagree about.
    for (const MethodDecl& mtd : module.methods) {
        const bool retVoid = isVoid(mtd.returnType);
        const QString ret = retVoid ? QStringLiteral("void")
                                    : surfaceType(module, mtd.returnType, QString(), false);
        const QString retQual = retVoid ? QStringLiteral("void")
                                        : surfaceType(module, mtd.returnType, qual, false);

        QStringList params;
        for (const ParamDecl& p : mtd.params) params << declParam(module, p.type, qs(p.name));

        auto emitArgs = [&]() {
            s << "    nlohmann::json _args = nlohmann::json::array();\n";
            for (const ParamDecl& p : mtd.params)
                s << "    _args.push_back(" << toWire(module, p.type, qs(p.name)) << ");\n";
        };

        // Sync
        {
            QStringList sig = params;
            sig << "logos::CallError* err";
            s << retQual << " " << className << "::" << qs(mtd.name) << "(" << sig.join(", ") << ") {\n";
        }
        emitArgs();
        s << "    logos::CallError _err;\n";
        s << "    nlohmann::json _r = logos::qt::invoke(m_bridge, \"" << qs(mtd.name) << "\", _args, &_err);\n";
        s << "    if (err) *err = _err;\n";
        s << "    else if (!_err.ok()) qWarning() << \"" << className << "::" << qs(mtd.name)
          << ": remote call failed:\" << QString::fromStdString(_err.message);\n";
        if (!retVoid)
            s << "    return " << fromWire(module, mtd.returnType, "_r", qual) << ";\n";
        else
            s << "    (void)_r;\n";
        s << "}\n\n";

        // Async. The caller's Timeout is threaded into the C ABI's timeout_ms
        // instead of being dropped.
        {
            const QString cb = retVoid ? QStringLiteral("std::function<void()>")
                                       : "std::function<void(" + ret + ")>";
            QStringList sig = params;
            sig << cb + " callback" << "Timeout timeout";
            s << "void " << className << "::" << qs(mtd.name) << "Async(" << sig.join(", ") << ") {\n";
        }
        s << "    if (!callback) return;\n";
        emitArgs();
        s << "    logos::qt::invokeAsync(m_bridge, \"" << qs(mtd.name) << "\", _args,\n";
        s << "        [callback](nlohmann::json _r) {\n";
        if (retVoid)
            s << "            (void)_r; callback();\n";
        else
            s << "            callback(" << fromWire(module, mtd.returnType, "_r", qual) << ");\n";
        s << "        }, timeout.ms);\n";
        s << "}\n\n";
    }

    return c;
}
