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

// ── the provider rejection codes ────────────────────────────────────────────
//
// The CLOSED SET of `code` values that mark a result as a provider REFUSAL
// rather than a value, and the single source of truth for the detector emitted
// below. One named constant per repo is the compromise this file's own header
// argues for: the detector body is DUPLICATED across generators on purpose
// (see the comment at its emission site), so the least this repo can do is
// hold the vocabulary in one place where a drift is visible.
//
// Why a closed set rather than "any {code,message,origin} object": a method may
// legitimately RETURN a three-string map, and an `any` return certainly can.
// Matching the shape alone would let user data impersonate a refusal.
//
//   "dispatch_failed" — the provider ran and refused the argument VALUES.
//   "invalid_args"    — wrong argument COUNT. Emitted today by logos-cpp-sdk's
//                       cdylib dispatch and logos-rust-sdk's args::invalid_args,
//                       and until now detected by nobody: an arity error read
//                       back as a successful call returning a map.
//   "unknown_method"  — nothing emits this yet, deliberately listed anyway.
//                       An unknown method is currently answered with a bare
//                       null (logos-protocol logos_protocol.h), and fixing that
//                       is a provider-contract change. Widening a detector is
//                       backwards-compatible on its own; a new provider code
//                       shipped against narrow detectors would arrive as DATA.
const char* const kRejectionCodes[] = {
    "dispatch_failed", "invalid_args", "unknown_method",
};

// `<var> != "a" && <var> != "b" && ...` over kRejectionCodes.
QString rejectionCodeMismatch(const QString& var, const QString& joinIndent)
{
    QStringList terms;
    for (const char* code : kRejectionCodes)
        terms << var + " != \"" + QString::fromLatin1(code) + "\"";
    return terms.join("\n" + joinIndent + "&& ");
}

// ── records ─────────────────────────────────────────────────────────────────

bool isRecordName(const ModuleDecl& m, const std::string& n)
{
    for (const TypeDecl& t : m.types) if (t.name == n) return true;
    return false;
}

// Does this type name a record DIRECTLY — `Foo`, and not `[Foo]` or
// `{tstr: Foo}`?
//
// It used to answer the composite shapes too (a `RecShape` of None / Scalar /
// List / Map), because `QList<Foo>` and `QMap<QString, Foo>` each had their own
// hand-written encode and decode loop beside the generic ones. That duplication
// was the whole of defect D2: those two loops INLINED their source expression
// instead of passing it as a lambda argument, so `{tstr: {tstr: Foo}}` emitted
// `for (auto __i = __i.value().cbegin(); ...)` — `__i` in its own initialiser —
// and `?{tstr: Foo}` emitted `*__c.cbegin()`, which parses as `*(__c.cbegin())`
// and asks a std::optional for an iterator. Neither compiles; no fixture had
// either shape.
//
// The generic container loops below already produce byte-identical code for
// `[Foo]` and `{tstr: Foo}` — they recurse, and the recursion lands on the
// scalar record case, which is this one. So the special cases are gone and there
// is now exactly ONE list loop, ONE map loop and ONE optional wrapper per
// direction, each taking its source as a PARAMETER. A shape can no longer
// compile by luck, because there is only one shape.
bool isRecordType(const ModuleDecl& m, const TypeExpr& te)
{
    return te.kind == TypeExpr::Named && isRecordName(m, te.name);
}

// QUALIFIED BY THE WRAPPER CLASS, and it has to be.
//
// These are file-scope `static` functions, and the umbrella
// (logos-cpp-sdk generator_lib.cpp) amalgamates every generated `<name>_api.cpp`
// into ONE translation unit by #including them from logos_sdk.cpp. Two
// contracts in one module that declare a record of the same name therefore
// emitted two `recFromWire_Blob` differing only in return type — "ambiguating
// new declaration", and the module does not compile.
//
// That is not a hypothetical shape: it is what a PROXY is. A module that binds
// an interface and also declares the two providers of that interface as
// dependencies gets three wrappers for one contract, so any record at all
// collides three ways. It went unnoticed because the only Qt-consumer fixture
// with records had a single contract in it.
//
// `ownerOf(qual)` is the class name and is already in scope at every emission
// site (it names the wrapper in the decode diagnostics), so the qualification
// costs no new plumbing.
QString recToWireFn(const QString& owner, const QString& r)
{
    return "recToWire_" + owner + "_" + r;
}
QString recFromWireFn(const QString& owner, const QString& r)
{
    return "recFromWire_" + owner + "_" + r;
}

// Declared early: `toWire` below needs the class name and is emitted before
// the diagnostics helper that already derives it.
QString ownerOf(const QString& qual);

// ── surface types ───────────────────────────────────────────────────────────
//
// `qual` qualifies a nested record where class scope does not apply — a return
// type written before the `Class::` of a definition.
//
// The qualification is handed to lidlTypeToQt as a HOOK rather than pattern-
// matched on the finished string. It used to be the latter, matching the three
// shapes that could mention a record (`Point`, `QList<Point>`,
// `QMap<QString, Point>`); the widened table can also produce `?Point`,
// `QList<QList<Point>>` and `QMap<QString, ?Point>`, and a string match would
// have emitted those unqualified — which compiles inside the class and fails
// outside it, i.e. in exactly half the emission sites.
QString surfaceType(const ModuleDecl& m, const TypeExpr& te, const QString& qual, bool paramPosition)
{
    const QString t = lidlTypeToQt(te, [&](const QString& n) {
        return isRecordName(m, n.toStdString()) ? qual + n : n;
    });
    // The one position-dependent spelling on the shipped surface: a `result`
    // RETURN is `LogosResult`, but a `result` PARAMETER has always been
    // `QVariant` (the legacy param table simply had no LogosResult entry).
    // Reproduced deliberately — this generator's contract is that no call site
    // moves, so an accidental "improvement" here would be a break.
    if (paramPosition && t == "LogosResult") return QStringLiteral("QVariant");
    return t;
}

// By const-ref exactly where the shipped surface used it. QByteArray and
// LogosResult are by VALUE — that is what shipped, and widening it would change
// overload resolution and address-of at existing call sites. Records are
// structs and were always by const-ref.
//
// The typed containers and optionals are NEW spellings — no call site can have
// been written against them — so they take the convention their shape asks for
// rather than a frozen one: a QList / QMap / std::optional is a container, and
// containers here go by const-ref.
bool byRef(const ModuleDecl& m, const TypeExpr& te, const QString& cppType)
{
    if (isRecordType(m, te)) return true;
    if (cppType.startsWith("QList<") || cppType.startsWith("QMap<")
        || cppType.startsWith("std::optional<")) return true;
    return cppType == "QString" || cppType == "QStringList"
        || cppType == "QJsonArray" || cppType == "QVariantList" || cppType == "QVariantMap";
}

QString declParam(const ModuleDecl& m, const TypeExpr& te, const QString& name)
{
    const QString t = surfaceType(m, te, QString(), /*paramPosition=*/true);
    return byRef(m, te, t) ? "const " + t + "& " + name : t + " " + name;
}

// A field's `?T` as a TypeExpr, whichever of the two spellings the author used.
// `? name: T` sets the flag and leaves the type T; `name: ?T` leaves the flag
// false and makes the type an Optional. Building ONE shape here is what makes
// the two emit identical code.
TypeExpr fieldOptionalType(const FieldDecl& f)
{
    if (f.type.kind == TypeExpr::Optional) return f.type;
    TypeExpr o;
    o.kind = TypeExpr::Optional;
    o.elements.push_back(f.type);
    return o;
}

// The surface spelling of a record FIELD, honouring BOTH optionality spellings.
//
// `? name: T` and `name: ?T` mean the same thing and must produce identical
// code; only fieldIsOptional() reconciles them, so a backend that reads
// `f.optional` or `f.type.kind` directly is the drift this exists to prevent.
//
// An optional field is std::optional<T> — the same answer every other `?T` slot
// now gets, and the same one the std surface has always given. It used to be a
// bare QVariant, so a consumer reading `Profile.nickname` could not tell a
// `?tstr` from a `?uint` without asking the contract. `?any` stays QVariant:
// `any` is the one row the table keeps untyped, and QVariant already has
// exactly one empty inhabitant, so wrapping it would spell EMPTY twice.
QString fieldSurfaceType(const ModuleDecl& m, const FieldDecl& f, const QString& qual)
{
    if (fieldIsOptional(f))
        return surfaceType(m, fieldOptionalType(f), qual, /*paramPosition=*/true);
    return surfaceType(m, f.type, qual, /*paramPosition=*/true);
}

// ── the two conversion spellings ────────────────────────────────────────────

// True when this slot must be encoded/decoded ELEMENT BY ELEMENT rather than
// handed to toWire / fromWire<T> whole.
//
// Two reasons, and they are now one question:
//   * it mentions a RECORD — a struct with no Q_DECLARE_METATYPE;
//   * its Qt spelling is a TYPED container or a std::optional, which QVariant
//     handles worse still. logos::qvariantToNlohmann matches a CLOSED
//     userType() set (QByteArray, LogosResult, the integer types, QStringList,
//     QVariantList, QVariantMap, the QJson types). QList<qulonglong> matches
//     NONE of them, so `toWire(QVariant::fromValue(list))` serialises to JSON
//     null, and coming back `qvariant_cast<QList<qulonglong>>` of a
//     QVariantList yields an EMPTY list. Both directions, silently.
//
// QStringList / QVariantList / QVariantMap / QVariant are NOT in this set: they
// are in that closed set and cross whole, exactly as they always did.
bool needsElementLoop(const ModuleDecl& m, const TypeExpr& te)
{
    return isRecordType(m, te) || lidlQtNeedsElementLoop(te);
}

// An element whose decode goes through the strict leaf decoder — i.e. neither a
// record (which has its own generated codec) nor a nested container/optional
// (which recurses into a loop of its own).
bool isCheckedLeaf(const ModuleDecl& m, const TypeExpr& te)
{
    return !needsElementLoop(m, te);
}

QString toWire(const ModuleDecl& m, const TypeExpr& te, const QString& expr,
               const QString& qual)
{
    // The one structural leaf: a record is a struct with a generated codec.
    // `[Foo]`, `{tstr: Foo}` and `?Foo` are NOT special-cased — the container
    // loops below recurse and land here.
    if (isRecordType(m, te))
        return recToWireFn(ownerOf(qual), qs(te.name)) + "(" + expr + ")";

    // The typed containers and optionals. There is deliberately ONE loop per
    // container kind, and every record shape reaches it by recursion.
    //
    // THE SOURCE IS A LAMBDA PARAMETER, never a local bound inside the body.
    // These loops NEST — `[[uint]]` puts one inside another — and every level
    // wants the same short names. A local (`const auto& __c = __c;`) or a
    // range-for over a name the loop itself declares is then self-referential:
    // it compiles, and it reads uninitialised memory. Passing the source as an
    // ARGUMENT evaluates it in the ENCLOSING scope, before the inner names
    // exist, so shadowing is harmless by construction. (Measured: the first
    // draft bound a local and three round-trip tests died on SIGTRAP.)
    if (lidlQtNeedsElementLoop(te)) {
        if (te.kind == TypeExpr::Array) {
            return "[&](const auto& __c){ nlohmann::json __acc = nlohmann::json::array(); "
                   "for (const auto& __e : __c) __acc.push_back("
                 + toWire(m, te.elements[0], "__e", qual) + "); return __acc; }(" + expr + ")";
        }
        if (te.kind == TypeExpr::Map) {
            return "[&](const auto& __c){ nlohmann::json __acc = nlohmann::json::object(); "
                   "for (auto __i = __c.cbegin(); __i != __c.cend(); ++__i) "
                   "__acc[__i.key().toStdString()] = "
                 + toWire(m, te.elements[1], "__i.value()", qual) + "; return __acc; }(" + expr + ")";
        }
        if (te.kind == TypeExpr::Optional) {
            // EMPTY is JSON null, the wire's single empty inhabitant, which
            // Codec<std::optional<T>> reads back as nullopt. A record FIELD
            // omits its key instead — see the field emission below; that is a
            // property of named slots, not of `?T`.
            return "[&](const auto& __c){ return __c.has_value() ? nlohmann::json("
                 + toWire(m, optionalValueType(te), "*__c", qual)
                 + ") : nlohmann::json(nullptr); }(" + expr + ")";
        }
    }
    // QVariant::fromValue makes the argument exactly ONE value — a bare
    // QVariantList-typed arg would otherwise be spread across the args array
    // (the historical "typed arrays arrive empty over the Qt path" bug). It is
    // a no-op for an already-QVariant (`any`) argument.
    return "logos::qt::toWire(QVariant::fromValue(" + expr + "))";
}

// ── the decode-error sink ───────────────────────────────────────────────────
//
// THE NAME every emitted decode writes its first rejection into: a
// `std::string*`, possibly null, declared by whatever is evaluating the decode
// expression (a record codec's parameter, a method body, a callback).
//
// Threaded as a NAME rather than as another argument because a decode
// expression is a tree of `[&]` lambdas of unbounded depth, and `[&]` makes an
// enclosing declaration visible at every level of it for free. Every emission
// site below declares it — grep for `declareSink`.
//
// WHY IT EXISTS. Element checking without it is only half a fix: a container
// whose elements were REFUSED and a container the provider legitimately sent
// empty came back as the same value, and `err.ok()` was true for both. A
// thousand-element list with one bad element became `[]`, reported as success.
// The qWarning said so in the log, which is not a channel a caller can branch
// on — and the std consumer of the same contract THREW for that input, so the
// two surfaces disagreed about whether a mistyped element is an error at all.
const char* const kSink = "__derr";

// `owner` names the wrapper class in a diagnostic; `te` is spelled in LIDL,
// because the reader of a module log is looking at a contract, not at Qt.
QString ownerOf(const QString& qual)
{
    return qual.endsWith("::") ? qual.left(qual.size() - 2) : qual;
}

// The rejection statement pair: record it on the error channel, say it in the
// log, then leave. `__why` is the codec's own sentence and must be in scope.
//
// BOTH, not one or the other. The qWarning is what a developer watching a
// module sees without changing any code; the sink is what a caller who passed
// `&err` can branch on. Dropping either one takes back half of the fix.
QString rejectStmt(const QString& owner, const QString& lidl, const QString& what,
                   const QString& bail)
{
    const QString head = owner + ": rejected a `" + lidl + "` " + what + ":";
    return QString("logos::qt::noteDecodeError(%1, \"%2 \" + __why); qWarning() << \"%2\" "
                   "<< QString::fromStdString(__why); %3")
        .arg(kSink, head, bail);
}

// The strict decode of ONE element, as a statement that assigns `dst` or bails
// out of the surrounding loop with `bail`.
//
// This is where the "no element type-checking" defect is closed. A leaf goes
// through logos::qt::tryFromWire, i.e. through THE CODEC — the same rule the
// std consumer and the provider apply — so `["x", 5]` read as `[uint]` is
// REJECTED with the codec's own sentence instead of arriving as [0, 5]. The
// whole container is refused, not the one element: a container that silently
// changed length would be the same class of lie in a different shape.
//
// `__why` is NOT declared here. The enclosing container loop declares it once
// and the shape check uses it too, so declaring it per element would shadow
// that one — legal, and a -Wshadow warning in generated code.
QString decodeElementStmt(const ModuleDecl& m, const TypeExpr& te, const QString& qual,
                          const QString& src, const QString& dst, const QString& bail);

// A CONTAINER whose Qt spelling crosses the QVariant boundary WHOLE, so it
// gets no element loop: `[tstr]` (QStringList), `[any]` (QVariantList),
// `{tstr: any}` (QVariantMap).
//
// It still declares a SHAPE, and `[tstr]` still declares an element type — so
// its decode is checked, just in one call instead of a loop:
// logos::qt::tryFromWire routes it through the codec
// (`std::vector<std::string>` / a shape assertion), which is the same rule the
// element decoder applies. `needsElementLoop` is about how a value crosses the
// QVariant boundary — an ENCODE property, decided by qvariantToNlohmann's
// closed userType() set — and using it to decide DECODE strictness is what
// left these three slots lenient.
//
// The tell that this was an accident rather than a contract: these very types
// are ALREADY checked one level down. `?[tstr]` and `[[tstr]]` decode their
// `[tstr]` through tryFromWire, because there `[tstr]` is an ELEMENT and
// isCheckedLeaf is true for it. Only the BARE slot was lenient, so one
// contract got two answers for the same payload depending on whether anything
// was wrapped around it.
bool isWholeCrossingContainer(const ModuleDecl& m, const TypeExpr& te)
{
    if (needsElementLoop(m, te)) return false;
    return te.kind == TypeExpr::Array || te.kind == TypeExpr::Map;
}

// Does the decode of `te` REJECT a value that does not match the contract,
// rather than coercing it?
//
// True for everything STRUCTURAL — a record (its own codec, which checks its
// shape and every field), a typed container or optional (its element loop),
// and the whole-crossing containers above (one checked call).
//
// False for a bare SCALAR slot, and that is deliberate and unchanged: `-> tstr`
// handed a number still answers "" rather than failing. That leniency is the
// shipped contract of BOTH consumer surfaces — logos_qt_wire.h says so for this
// one, and logos-cpp-sdk's lp emitter (lpFromJsonExpr) independently answers
// 0 / "" / false for the same mismatch — so tightening it is a separate
// decision about a much wider surface, not part of closing this one.
// tests/qt-generator/roundtrip_tu.cpp pins the leniency deliberately, so that
// it is a choice on the record rather than an omission.
bool decodeIsChecked(const ModuleDecl& m, const TypeExpr& te)
{
    return needsElementLoop(m, te) || isWholeCrossingContainer(m, te);
}

// Does the decode of `te` mention the sink? Exactly when it can reject: the
// two questions have one answer, and naming them separately is how they would
// drift into a declared-but-unused (or used-but-undeclared) `__derr`.
bool decodeUsesSink(const ModuleDecl& m, const TypeExpr& te)
{
    return decodeIsChecked(m, te);
}

QString fromWire(const ModuleDecl& m, const TypeExpr& te, const QString& json, const QString& qual)
{
    // A record's own codec, which takes the sink so a rejection INSIDE a record
    // field reaches the same channel as one in a bare container.
    if (isRecordType(m, te))
        return recFromWireFn(ownerOf(qual), qs(te.name)) + "(" + json + ", " + kSink + ")";

    const QString cpp = surfaceType(m, te, qual, /*paramPosition=*/false);
    // Same loops in reverse, and the same rule about the source: it is a lambda
    // PARAMETER, so a nested level can reuse these names without the initialiser
    // ever naming something the body itself declares.
    if (lidlQtNeedsElementLoop(te)) {
        const QString bail = "return __acc;";
        const QString owner = ownerOf(qual);
        const QString lidl = lidlTypeToLidlText(te);
        if (te.kind == TypeExpr::Array) {
            // The SHAPE is part of the declared type, so a non-array is a
            // rejection and not an empty list. `__why` is declared once and
            // reused by the element decode inside the loop.
            return "[&](const nlohmann::json& __s){ " + cpp + " __acc; std::string __why; "
                   "if (!logos::qt::tryRequireArray(__s, &__why)) { "
                 + rejectStmt(owner, lidl, "value", bail) + " } for (const auto& __e : __s) { "
                 + decodeElementStmt(m, te.elements[0], qual, "__e", "__v", "return " + cpp + "();")
                 + " __acc.push_back(__v); } return __acc; }(" + json + ")";
        }
        if (te.kind == TypeExpr::Map) {
            return "[&](const nlohmann::json& __s){ " + cpp + " __acc; std::string __why; "
                   "if (!logos::qt::tryRequireObject(__s, &__why)) { "
                 + rejectStmt(owner, lidl, "value", bail)
                 + " } for (auto __i = __s.begin(); __i != __s.end(); ++__i) { "
                   "const nlohmann::json& __e = __i.value(); "
                 + decodeElementStmt(m, te.elements[1], qual, "__e", "__v", "return " + cpp + "();")
                 + " __acc.insert(QString::fromStdString(__i.key()), __v); } return __acc; }("
                 + json + ")";
        }
        if (te.kind == TypeExpr::Optional) {
            // JSON null IS the empty state — no shape to check, and nothing to
            // reject. An absent record KEY lands here too: the field decode
            // below only enters on `contains`, so absent keeps the default,
            // which is that same empty optional.
            //
            // `__why` is declared only when the value decodes through the leaf
            // decoder; a nested container or a record brings its own.
            const TypeExpr vt = optionalValueType(te);
            const QString why = isCheckedLeaf(m, vt) ? QStringLiteral("std::string __why; ")
                                                     : QString();
            return "[&](const nlohmann::json& __s){ if (__s.is_null()) return " + cpp + "(); " + why
                 + decodeElementStmt(m, vt, qual, "__s", "__v", "return " + cpp + "();")
                 + " return " + cpp + "(__v); }(" + json + ")";
        }
    }
    // A whole-crossing container: no loop, but still a checked decode. Same
    // reject-and-return-empty shape the loops use, so a wrong-shaped `[tstr]`
    // and a wrong-shaped `[uint]` answer the caller the same way.
    if (isWholeCrossingContainer(m, te)) {
        const QString owner = ownerOf(qual);
        const QString lidl = lidlTypeToLidlText(te);
        return "[&](const nlohmann::json& __s){ " + cpp + " __acc; std::string __why; "
               "if (!logos::qt::tryFromWire(__s, __acc, &__why)) { "
             + rejectStmt(owner, lidl, "value", "return " + cpp + "();")
             + " } return __acc; }(" + json + ")";
    }

    // A bare SCALAR. The lenient narrowing, deliberately — see decodeIsChecked.
    return "logos::qt::fromWire<" + cpp + ">(" + json + ")";
}

QString decodeElementStmt(const ModuleDecl& m, const TypeExpr& te, const QString& qual,
                          const QString& src, const QString& dst, const QString& bail)
{
    const QString cpp = surfaceType(m, te, qual, /*paramPosition=*/false);
    if (!isCheckedLeaf(m, te)) {
        // A record or a nested container: it owns its own decode (and its own
        // checking, and its own rejection, one level down).
        return cpp + " " + dst + " = " + fromWire(m, te, src, qual) + ";";
    }
    return cpp + " " + dst + "{}; if (!logos::qt::tryFromWire(" + src + ", " + dst + ", &__why)) { "
         + rejectStmt(ownerOf(qual), lidlTypeToLidlText(te), "element", bail) + " }";
}

// `std::string* __derr = <init>;` — the declaration every context that evaluates
// a decode expression needs. Emitted only where the decode can actually reject
// (see decodeUsesSink), because an unused one is a warning in generated code.
QString declareSink(const QString& init)
{
    return QString("std::string* %1 = %2;").arg(kSink, init);
}

// A record's field type as the emitter sees it — the two optionality spellings
// reconciled into one shape, exactly as fieldSurfaceType does for the surface.
TypeExpr effectiveFieldType(const FieldDecl& f)
{
    return fieldIsOptional(f) ? fieldOptionalType(f) : f.type;
}

// GONE: `recordDecodeUsesSink`. It answered "does this record's decode name the
// sink?", so that a record of plain scalars could leave the parameter unnamed
// and avoid an unused-parameter warning. Every record's decode now names it —
// the SHAPE check alone can reject — so the question has one answer and the
// parameter is always named. A predicate that is constantly true is a place for
// a future reader to wonder whether it might not be.

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

// Does any slot in this contract materialise a std::optional on the surface?
// Gates the generated `#include <optional>`, so a contract with no optional —
// or one whose only optionals are `?any`, which stays QVariant — keeps its
// header byte-for-byte what it was.
bool typeUsesStdOptional(const TypeExpr& te)
{
    if (te.kind == TypeExpr::Optional && lidlQtNeedsElementLoop(te)) return true;
    for (const TypeExpr& e : te.elements)
        if (typeUsesStdOptional(e)) return true;
    return false;
}

bool moduleUsesStdOptional(const ModuleDecl& m)
{
    for (const TypeDecl& t : m.types)
        for (const FieldDecl& f : t.fields) {
            const TypeExpr eff = fieldIsOptional(f) ? fieldOptionalType(f) : f.type;
            if (typeUsesStdOptional(eff)) return true;
        }
    for (const MethodDecl& md : m.methods) {
        if (typeUsesStdOptional(md.returnType)) return true;
        for (const ParamDecl& p : md.params) if (typeUsesStdOptional(p.type)) return true;
    }
    for (const EventDecl& ed : m.events)
        for (const ParamDecl& p : ed.params) if (typeUsesStdOptional(p.type)) return true;
    return false;
}

}  // namespace

// ── the retired `-> ?T` gate ────────────────────────────────────────────────
//
// This backend used to REFUSE a contract with an optional RETURN, because an
// empty `?T` is JSON null on the wire and null was also how a FAILED call
// reported itself to a non-Rust caller — so "found nothing" and "the call
// failed" were the same answer.
//
// The ambiguity is not GONE. It is RESOLVABLE, by opting in — which is a real
// difference from a refusal, and a different claim from "the two can never be
// confused". Stated exactly, because the gate was lifted on the strength of it:
//
//   * FAILURE DOES NOT TRAVEL IN THE VALUE. A generated body calls
//     logos::qt::invoke -> logos::LpClient::invoke, which decides success from
//     the C ABI's return code (`rc == LP_OK`) and never from the result's
//     null-ness — see logos-cpp-sdk's logos_lp_client.h. It travels on a second
//     channel, `logos::CallError`, filled from a source independent of the
//     value — and, since the element loops gained a sink, that channel also
//     carries a decode rejection.
//   * THAT CHANNEL IS DEFAULTED OFF. The sync signature ends
//     `logos::CallError* err = nullptr`, so a caller who passes nothing still
//     sees ONE value — an empty optional — for both "found nothing" and "the
//     call failed", plus a qWarning no code can branch on. The same choice is
//     spelled as an overload on the async side: `<name>Async` hands over a bare
//     value and cannot distinguish them, `<name>AsyncResult` carries the error.
//   * THE EMPTY ANSWER IS EXPRESSIBLE, unconditionally. `?T` is
//     std::optional<T> now, so the empty answer is std::nullopt — distinct from
//     T{}, which is what a bare QVariant return could not express. This one
//     holds whether or not the caller takes the error.
//
// One null-means-failure rule does survive, and it is worth naming rather than
// leaving for someone to rediscover: logoscore's core_service turns a null
// result into METHOD_FAILED for `logosctl module call`. It is a CLI-layer rule
// about untyped invocation, it is not on this path, and `-> any` — which may
// legitimately answer null and has never been gated — already trips it. Gating
// `-> ?T` while allowing `-> any` was the inconsistency, not the protection.
// ── header ──────────────────────────────────────────────────────────────────

QString lidlMakeQtConsumerHeader(const ModuleDecl& module,
                                 const QString& moduleName,
                                 const QString& className,
                                 QtConsumerBind bind,
                                 QtConsumerBinding binding)
{
    (void)moduleName;
    const bool noApi = (binding == QtConsumerBinding::ExplicitOrigin);
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
    if (moduleUsesStdOptional(module)) s << "#include <optional>\n";
    s << "#include \"logos_types.h\"\n";
    // The LogosAPI-free flavour names neither LogosAPI nor LogosAPIClient, so
    // it includes neither. `Timeout` (every method's trailing deadline) then
    // has to be named directly: it lives in logos_mode.h, which the consumer
    // header only ever got transitively, through logos_api_client.h.
    if (noApi) {
        s << "#include \"logos_mode.h\"\n";
    } else {
        s << "#include \"logos_api.h\"\n";
        s << "#include \"logos_api_client.h\"\n";
    }
    s << "#include \"logos_call_error.h\"\n";
    s << "#include \"logos_async_result.h\"\n";
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
                s << "        " << fieldSurfaceType(module, f, QString())
                  << " " << qs(f.name) << "{};\n";
            s << "    };\n";
        }
        s << "\n";
    }

    // Four constructors, one per (target x origin) combination. The two
    // ExplicitOrigin ones take the CONSUMING module's own name as their first
    // argument; nothing in the class can derive it.
    if (noApi) {
        if (bind == QtConsumerBind::Bound)
            s << "    explicit " << className
              << "(const QString& origin, const QString& target);\n\n";
        else
            s << "    explicit " << className << "(const QString& origin);\n\n";
    } else if (bind == QtConsumerBind::Bound) {
        s << "    explicit " << className << "(LogosAPI* api, const QString& moduleName);\n\n";
    } else {
        s << "    explicit " << className << "(LogosAPI* api);\n\n";
    }

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

        // Both trailing and both defaulted, so every existing call site — up to
        // and including the ones that already pass `&err` positionally — is
        // unchanged. The deadline is APPENDED rather than sitting next to the
        // value args (where the async overloads carry it) for exactly that
        // reason; the seam has taken one since `logos::qt::invoke` grew its
        // timeoutMs parameter, and the body simply defaulted it.
        QStringList sync = params;
        sync << "logos::CallError* err = nullptr" << "Timeout timeout = Timeout()";
        s << "    " << ret << " " << qs(mtd.name) << "(" << sync.join(", ") << ");\n";

        const QString cb = (ret == "void") ? QStringLiteral("std::function<void()>")
                                           : "std::function<void(" + ret + ")>";
        QStringList async = params;
        async << cb + " callback" << "Timeout timeout = Timeout()";
        s << "    void " << qs(mtd.name) << "Async(" << async.join(", ") << ");\n";

        // The result-carrying async. `<name>Async` above hands the callback a
        // bare value, so a failed call is INDISTINGUISHABLE from a provider
        // that legitimately returned 0 / "" / false — the ambiguity the sync
        // `CallError*` exists to resolve. This one delivers both.
        //
        // A DISTINCT NAME, not an overload of `<name>Async`: two overloads
        // differing only in std::function<void(T)> vs
        // std::function<void(AsyncResult<T>)> are ambiguous for a generic
        // lambda (`[](auto v){...}` is invocable with either), which would
        // break existing call sites. `void` needs no special case — it is
        // AsyncResult<void>, the error-only specialisation.
        QStringList asyncResult = params;
        asyncResult << "std::function<void(logos::AsyncResult<" + ret + ">)> callback"
                    << "Timeout timeout = Timeout()";
        s << "    void " << qs(mtd.name) << "AsyncResult(" << asyncResult.join(", ") << ");\n";
    }

    s << "\nprivate:\n";
    if (!noApi) s << "    LogosAPI* m_api;\n";
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
                                 QtConsumerBind bind,
                                 QtConsumerBinding binding)
{
    const bool noApi = (binding == QtConsumerBinding::ExplicitOrigin);
    QString c;
    QTextStream s(&c);

    s << "#include \"" << headerBaseName << "\"\n\n";
    s << "#include <QDebug>\n";
    s << "#include <nlohmann/json.hpp>\n";
    s << "#include \"logos_qt_lp_bridge.h\"\n\n";

    const QString qual = className + "::";

    // A provider that RAN and rejected well-formed arguments answers the
    // canonical {code, message, origin} envelope as its RESULT, not as a
    // transport error — logos_protocol.h states so explicitly, and says the
    // generated wrappers are what fold it. Without this the rejection reaches
    // the return conversion, which erases it: a rejected `[uint]` call comes
    // back as [] — the whole list, not a bad element — with error.ok() true.
    //
    // The QVariant twin of this lives in the cpp-generator's Qt emitter; this
    // is the nlohmann one, because the lp seam speaks JSON. Kept emitted (not
    // shared through a header) so both emitters stay independent, exactly as
    // the QVariant one already is.
    //
    // The match is NARROW — those three fields, all strings, and a code from
    // the closed kRejectionCodes set above — for the same reason
    // logos_rpc_status.h's isUnauthorizedSentinel is exact: an `any` or map
    // return carrying user data must never false-match. Any other code, a
    // 2- or 4-key object, or a non-string value all stay DATA.
    // Only reachable from a method body, so a contract with no methods must not
    // emit it: an unused function in an anonymous namespace is a
    // -Wunused-function warning.
    //
    // The include guard is load-bearing, not habit. These wrapper .cpp files are
    // not compiled on their own — the generated umbrella (logos_sdk.cpp)
    // `#include`s every one of them, so a module with two dependencies puts two
    // copies of this function in ONE translation unit. The anonymous namespace
    // does not save it: all anonymous namespaces in a TU are the SAME namespace,
    // so the second copy is a redefinition and the build dies in generated code.
    // (A module with a single dependency compiles fine, which is why this only
    // shows up on the real multi-dependency consumers.)
    //
    // The macro is deliberately distinct from the cpp-generator emitter's
    // LOGOS_GENERATED_DISPATCH_REJECTION: that one guards the QVariant twin of
    // this function, and while the two surfaces coexist a single TU can end up
    // holding one wrapper of each flavour. Sharing the macro would silence the
    // wrong definition and leave a rejection unfolded.
    if (!module.methods.empty()) {
        s << "#ifndef LOGOS_GENERATED_DISPATCH_REJECTION_JSON\n";
        s << "#define LOGOS_GENERATED_DISPATCH_REJECTION_JSON\n\n";
        s << "namespace {\n\n";
        s << "bool logosDispatchRejectionJson(const nlohmann::json& v, logos::CallError& out)\n";
        s << "{\n";
        s << "    if (!v.is_object() || v.size() != 3) return false;\n";
        s << "    auto code = v.find(\"code\"), message = v.find(\"message\"), origin = v.find(\"origin\");\n";
        s << "    if (code == v.end() || message == v.end() || origin == v.end()) return false;\n";
        s << "    if (!code->is_string() || !message->is_string() || !origin->is_string()) return false;\n";
        s << "    const std::string _code = code->get<std::string>();\n";
        s << "    if (" << rejectionCodeMismatch("_code", "        ") << ") return false;\n";
        s << "    out.code = _code;\n";
        s << "    out.message = message->get<std::string>();\n";
        s << "    out.origin = origin->get<std::string>();\n";
        s << "    return true;\n";
        s << "}\n\n";
        s << "} // namespace\n\n";
        s << "#endif  // LOGOS_GENERATED_DISPATCH_REJECTION_JSON\n\n";
    }

    // The other half of the error channel: a rejection the generated ELEMENT
    // loops recorded, folded into the caller's CallError.
    //
    // A decode rejection is NOT a transport failure and not a provider
    // rejection — the call reached the module, the module answered, and the
    // answer does not match the contract this wrapper was generated from. It
    // needs its own code, and "decode_failed" is it. (logos_call_error.h lists
    // the codes its own canonical constructors produce; this one is detected
    // HERE, by generated code, exactly as "dispatch_failed" is above, so it has
    // no constructor there to call.)
    //
    // Never overwrites an error already on the channel: a transport failure or
    // a provider rejection came first and describes the real cause, and the
    // garbage that follows one is a consequence, not a second fault. `origin`
    // is the TARGET module — the one whose answer was refused — read from
    // m_moduleName so a runtime-bound wrapper names the module it actually
    // called.
    //
    // Its own include guard, for the same reason the one above has one: the
    // umbrella puts every dependency's wrapper .cpp in ONE translation unit.
    // Emitted only when some method's return can reject, or it is an unused
    // static function.
    const bool anyReturnDecodes = [&] {
        for (const MethodDecl& mtd : module.methods)
            if (!isVoid(mtd.returnType) && decodeUsesSink(module, mtd.returnType)) return true;
        return false;
    }();
    if (anyReturnDecodes) {
        s << "#ifndef LOGOS_GENERATED_DECODE_FAILURE_JSON\n";
        s << "#define LOGOS_GENERATED_DECODE_FAILURE_JSON\n\n";
        s << "namespace {\n\n";
        s << "void logosNoteDecodeFailure(const std::string& why, const std::string& origin,\n";
        s << "                            logos::CallError& out)\n";
        s << "{\n";
        s << "    if (why.empty() || !out.ok()) return;\n";
        s << "    out.code = \"decode_failed\";\n";
        s << "    out.message = why;\n";
        s << "    out.origin = origin;\n";
        s << "}\n\n";
        s << "} // namespace\n\n";
        s << "#endif  // LOGOS_GENERATED_DECODE_FAILURE_JSON\n\n";
    }

    // Record <-> canonical JSON. Declared up front so records can reference
    // each other (and themselves, through a list field) in any order.
    if (!module.types.empty()) {
        for (const TypeDecl& t : module.types) {
            s << "static nlohmann::json " << recToWireFn(ownerOf(qual), qs(t.name))
              << "(const " << qual << qs(t.name) << "& v);\n";
            // The sink is DEFAULTED here and only here — one default per
            // function, and this declaration is what a hand-written caller
            // (the round-trip test) sees, so `recFromWire_Bag(j)` keeps
            // working while the generated call sites all pass it through.
            s << "static " << qual << qs(t.name) << " " << recFromWireFn(ownerOf(qual), qs(t.name))
              << "(const nlohmann::json& w, std::string* " << kSink << " = nullptr);\n";
        }
        s << "\n";
        for (const TypeDecl& t : module.types) {
            s << "static nlohmann::json " << recToWireFn(ownerOf(qual), qs(t.name))
              << "(const " << qual << qs(t.name) << "& v) {\n";
            s << "    nlohmann::json __j = nlohmann::json::object();\n";
            for (const FieldDecl& f : t.fields) {
                const QString fv = "v." + qs(f.name);
                if (fieldIsOptional(f)) {
                    // An EMPTY optional omits its key rather than writing null.
                    // Both spell "absent" to a reader (Codec<std::optional<T>>
                    // maps a missing key and an explicit null alike), but
                    // omitting is what the C++ and Rust encoders already do, and
                    // sending the key back means a round-tripped record is
                    // byte-identical to the one that was received.
                    //
                    // The emptiness TEST follows the field's own spelling:
                    // `.has_value()` for a std::optional, `.isValid()` for the
                    // `?any` slot that is still a QVariant. Those are the only
                    // two types this surface produces for an optional field.
                    const TypeExpr ot = fieldOptionalType(f);
                    if (lidlQtNeedsElementLoop(ot)) {
                        s << "    if (" << fv << ".has_value()) __j[\"" << qs(f.name) << "\"] = "
                          << toWire(module, fieldValueType(f), "*" + fv, qual) << ";\n";
                    } else {
                        s << "    if (" << fv << ".isValid()) __j[\"" << qs(f.name) << "\"] = "
                          << toWire(module, f.type, fv, qual) << ";\n";
                    }
                } else {
                    s << "    __j[\"" << qs(f.name) << "\"] = "
                      << toWire(module, f.type, fv, qual) << ";\n";
                }
            }
            s << "    return __j;\n";
            s << "}\n\n";

            // A record is a STRUCTURAL slot, and it is checked like one: its
            // SHAPE, and every field that is PRESENT. Its std twin —
            // logos_codec.h's `Codec<Record>::from`, which the cdylib
            // dispatch, the Rust provider and the plain wire all run — does
            // exactly that, and a Qt consumer of the same contract that
            // answered a default-constructed struct instead was the record
            // shape of the silent-empty-with-ok this layer exists to close:
            // the caller gets a fully-formed record carrying values the
            // provider never sent, with no way to tell which ones.
            //
            // THE GRANULARITY IS THE SLOT, not the record, and that is
            // deliberate: a rejected FIELD keeps its default and records the
            // rejection, exactly as a rejected ELEMENT leaves its container
            // empty and records it. That rule is already here for container
            // fields (roundtrip_tu.cpp's ElementCheckingReachesNestedContainers
            // pins it); giving scalar fields a different one would put two
            // answers in one function. A wrong SHAPE is not a slot at all —
            // there are no fields to keep — so it returns the default record,
            // which is also what Codec<Record>::from's typeError amounts to.
            //
            // WHAT STAYS LENIENT, deliberately: a MISSING key keeps the
            // field's default and says nothing. Absence is a question about
            // the MESSAGE — did the peer send this key — not about whether a
            // value matches its declared type, and both record codecs in this
            // stack (this one and logos-cpp-sdk's lp emitter) have always
            // answered it that way. Turning it into a refusal is a
            // wire-compatibility change far wider than this defect.
            // roundtrip_tu.cpp pins it as a choice rather than an omission.
            //
            // The sink parameter is therefore always NAMED: every record's
            // decode can reject now, if only on its shape.
            s << "static " << qual << qs(t.name) << " " << recFromWireFn(ownerOf(qual), qs(t.name))
              << "(const nlohmann::json& w, std::string* " << kSink << ") {\n";
            s << "    " << qual << qs(t.name) << " __out;\n";
            s << "    std::string __why;\n";
            s << "    if (!logos::qt::tryRequireObject(w, &__why)) { "
              << rejectStmt(ownerOf(qual), qs(t.name), "value", "return __out;") << " }\n";
            for (const FieldDecl& f : t.fields) {
                const QString src = "w.at(\"" + qs(f.name) + "\")";
                // An optional field decodes through the optional path: a JSON
                // null becomes the empty optional (or, for `?any`, the invalid
                // QVariant, which IS that spelling's empty inhabitant), so an
                // absent key and a null key land on the same value — and the
                // surrounding `contains` guard keeps a missing key at the
                // default, which is that same empty state.
                const TypeExpr ft = effectiveFieldType(f);
                s << "    if (w.contains(\"" << qs(f.name) << "\")) ";
                if (decodeIsChecked(module, ft)) {
                    // A record, a container or an optional: its own decode
                    // already checks and already writes to this same sink.
                    s << "__out." << qs(f.name) << " = "
                      << fromWire(module, ft, src, qual) << ";\n";
                } else {
                    // A bare SCALAR field. This is the half of the element fix
                    // that was missing: inside ONE record a rejected `[uint]`
                    // field was reported while a mistyped `uint` field beside
                    // it was silently zeroed, from the same wire object, on the
                    // same error channel.
                    //
                    // A slot-level scalar (a `-> tstr` RETURN) keeps the
                    // lenient narrowing; a scalar FIELD does not, because it is
                    // part of a structure the wrapper is reconstructing and the
                    // caller cannot see which member was fabricated. See
                    // decodeIsChecked for where that line is drawn.
                    //
                    // `__why` is the function-scope one the shape check
                    // declared; every field reuses it, so no field shadows
                    // another.
                    // paramPosition=TRUE, i.e. exactly fieldSurfaceType's
                    // spelling — `__v` is assigned straight into the member, so
                    // the two must agree. They differ for one type: a bare
                    // `result` FIELD is declared QVariant (the legacy param
                    // table had no LogosResult entry, and this generator's
                    // contract is that no call site moves), while
                    // paramPosition=false would spell it LogosResult and not
                    // compile. No fixture had such a field, which is why the
                    // expression path this replaces never hit it either.
                    const QString cpp = surfaceType(module, ft, qual, /*paramPosition=*/true);
                    s << "{ " << cpp << " __v{}; if (logos::qt::tryFromWire(" << src
                      << ", __v, &__why)) __out." << qs(f.name) << " = __v; else { "
                      << rejectStmt(ownerOf(qual), lidlTypeToLidlText(ft),
                                    "field `" + qs(f.name) + "`", QString())
                      << "} }\n";
                }
            }
            s << "    return __out;\n";
            s << "}\n\n";
        }
    }

    // Constructor. Everything goes through m_bridge. There is deliberately no
    // eager api->getClient() here: it built a LogosAPIClient nothing called,
    // and constructing one opens a transport to the target AND one to
    // capability_module. It mints no token — token exchange is lazy, in
    // LogosAPIClient::mintAndCacheToken on first invoke.
    //
    // ExplicitOrigin threads `origin` STRAIGHT into the bridge lookup — the
    // parameter, unmodified, never a member of some other object. That is the
    // whole difference from forTarget, which reads the name off the LogosAPI it
    // was handed: a wrapper built on a borrowed api calls out under the
    // lender's identity, and the transport has no way to notice.
    if (noApi && bind == QtConsumerBind::Bound) {
        s << className << "::" << className << "(const QString& origin, const QString& target)\n";
        s << "    : m_moduleName(target),\n";
        s << "      m_bridge(logos::qt::LpBridge::forOrigin(origin, target)) {}\n\n";
    } else if (noApi) {
        s << className << "::" << className << "(const QString& origin)\n";
        s << "    : m_moduleName(QStringLiteral(\"" << moduleName << "\")),\n";
        s << "      m_bridge(logos::qt::LpBridge::forOrigin(origin, QStringLiteral(\""
          << moduleName << "\"))) {}\n\n";
    } else if (bind == QtConsumerBind::Bound) {
        s << className << "::" << className << "(LogosAPI* api, const QString& moduleName)\n";
        s << "    : m_api(api), m_moduleName(moduleName),\n";
        s << "      m_bridge(logos::qt::LpBridge::forTarget(api, moduleName)) {}\n\n";
    } else {
        s << className << "::" << className << "(LogosAPI* api)\n";
        s << "    : m_api(api),\n";
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
        // An event delivery has no error channel — nobody is waiting on a
        // return — so the sink is null and a rejected argument is reported the
        // only way it can be, in the log. The declaration still has to exist:
        // the decode expressions below name it.
        {
            bool anyArgDecodes = false;
            for (const ParamDecl& p : ev.params)
                if (decodeUsesSink(module, p.type)) anyArgDecodes = true;
            if (anyArgDecodes) s << "        " << declareSink("nullptr") << "\n";
        }
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
                s << "    _args.push_back(" << toWire(module, p.type, qs(p.name), qual) << ");\n";
        };

        // Can decoding this method's ANSWER reject? Only then does the body
        // grow an error sink and the fold below it. A method returning a
        // scalar, `any` or void emits exactly the lines it always did.
        const bool retDecodes = !retVoid && decodeUsesSink(module, mtd.returnType);

        // Sync. The caller's Timeout is threaded into the C ABI's timeout_ms
        // just as the async paths do.
        {
            QStringList sig = params;
            sig << "logos::CallError* err" << "Timeout timeout";
            s << retQual << " " << className << "::" << qs(mtd.name) << "(" << sig.join(", ") << ") {\n";
        }
        emitArgs();
        s << "    logos::CallError _err;\n";
        s << "    nlohmann::json _r = logos::qt::invoke(m_bridge, \"" << qs(mtd.name)
          << "\", _args, &_err, timeout.ms);\n";
        // Fold a provider rejection into the error channel BEFORE the return
        // table converts it, or the conversion erases it.
        s << "    if (_err.ok()) logosDispatchRejectionJson(_r, _err);\n";
        s << "    if (err) *err = _err;\n";
        s << "    else if (!_err.ok()) qWarning() << \"" << className << "::" << qs(mtd.name)
          << ": remote call failed:\" << QString::fromStdString(_err.message);\n";
        if (retDecodes) {
            // The decode runs into a NAMED local so its rejection can be folded
            // afterwards. It cannot be folded before: the rejection is only
            // known once the decode has walked the value.
            //
            // The fold is guarded on `err` — the error channel is opt-in, and a
            // caller who did not ask for one already gets the qWarning the
            // loops emit. That is the same rule the transport error above
            // follows, and it is exactly what makes the empty-optional /
            // failed-call ambiguity RESOLVABLE rather than resolved: pass
            // `&err`, and the two are distinct.
            s << "    std::string _derr;\n";
            s << "    " << declareSink("&_derr") << "\n";
            s << "    " << retQual << " _out = " << fromWire(module, mtd.returnType, "_r", qual)
              << ";\n";
            s << "    if (err) logosNoteDecodeFailure(_derr, m_moduleName.toStdString(), *err);\n";
            s << "    return _out;\n";
        } else if (!retVoid) {
            s << "    return " << fromWire(module, mtd.returnType, "_r", qual) << ";\n";
        } else {
            s << "    (void)_r;\n";
        }
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
        // The value-only callback has nowhere to put an error, so a rejection
        // is at least made visible in the module log rather than vanishing into
        // the return conversion below. `<name>AsyncResult` is the surface that
        // can actually REPORT it.
        s << "            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))\n";
        s << "                  qWarning() << \"" << className << "::" << qs(mtd.name)
          << "Async: remote call failed:\" << QString::fromStdString(_rej.message); }\n";
        if (retVoid) {
            s << "            (void)_r; callback();\n";
        } else {
            // Same reason as the rejection fold above: this callback takes a
            // bare value, so a decode rejection has nowhere to go but the log —
            // which is where the element loops already put it. The sink is
            // NULL rather than absent because the decode expression names it.
            // `<name>AsyncResult` is the surface that reports it.
            if (retDecodes) s << "            " << declareSink("nullptr") << "\n";
            s << "            callback(" << fromWire(module, mtd.returnType, "_r", qual) << ");\n";
        }
        s << "        }, timeout.ms);\n";
        s << "}\n\n";

        // Result-carrying async. Same argument encoding and the same return
        // decoding as the two above — the value it delivers is the one
        // `<name>Async` would have — and what it adds is the error that
        // callback has nowhere to put. On failure the value stays
        // default-constructed and `_res.error` says so.
        {
            QStringList sig = params;
            sig << "std::function<void(logos::AsyncResult<" + ret + ">)> callback"
                << "Timeout timeout";
            s << "void " << className << "::" << qs(mtd.name) << "AsyncResult("
              << sig.join(", ") << ") {\n";
        }
        s << "    if (!callback) return;\n";
        emitArgs();
        s << "    logos::qt::invokeAsyncResult(m_bridge, \"" << qs(mtd.name) << "\", _args,\n";
        // The target's name is CAPTURED, not read off `this`: the callback runs
        // after this function returns, and a wrapper is a copyable handle that
        // may not outlive the call.
        if (retDecodes)
            s << "        [callback, _target = m_moduleName.toStdString()]"
                 "(nlohmann::json _r, const logos::CallError& _err) {\n";
        else
            s << "        [callback](nlohmann::json _r, const logos::CallError& _err) {\n";
        s << "            logos::AsyncResult<" << ret << "> _res;\n";
        s << "            _res.error = _err;\n";
        // This is the surface that can report a rejection, and callers branch
        // on _res.ok(). Fold before the conversion below, or a rejected call
        // reports success with a default-constructed value.
        s << "            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);\n";
        if (retVoid) {
            s << "            (void)_r;\n";
        } else if (retDecodes) {
            // This IS the async error channel, so the rejection is reported
            // rather than only logged — unguarded, because a caller of this
            // overload asked for the error by choosing it.
            s << "            std::string _derr;\n";
            s << "            " << declareSink("&_derr") << "\n";
            s << "            _res.value = " << fromWire(module, mtd.returnType, "_r", qual)
              << ";\n";
            s << "            logosNoteDecodeFailure(_derr, _target, _res.error);\n";
        } else {
            s << "            _res.value = " << fromWire(module, mtd.returnType, "_r", qual) << ";\n";
        }
        s << "            callback(_res);\n";
        s << "        }, timeout.ms);\n";
        s << "}\n\n";
    }

    return c;
}
