#ifndef LOGOS_QT_WIRE_H
#define LOGOS_QT_WIRE_H

// ---------------------------------------------------------------------------
// The Qt <-> canonical-JSON EDGE — the whole conversion surface a generated
// Qt-typed consumer wrapper is allowed to use.
//
// Background. There used to be two consumer implementations for calling one
// module from another: a Qt-typed one with its own conversion tables, and an
// std-typed `lp` one over the logos-protocol C ABI. Two implementations of one
// behaviour, and their tables disagreed — the Qt SYNC and ASYNC return paths
// even converted the same type two different ways inside one generated file.
// The Qt wrapper is now a veneer: it keeps its types and converts here, then
// delegates to the lp path.
//
// THE RULE. A code generator may not emit a conversion. It emits CALLS naming a
// type; the conversion lives in the canonical converters that already own the
// Qt <-> wire mapping, all of which sit on the single deduped codec
// (logos-protocol's logos_codec.h):
//
//     logos::qvariantToNlohmann       Qt -> canonical JSON
//     logos::nlohmannToQVariant       canonical JSON -> Qt
//     logos::jsonToLogosResult        canonical JSON -> LogosResult
//
// `fromWire<T>` is not a per-type table. It is one canonical decode followed by
// Qt's own uniform narrowing (`qvariant_cast`) — the same narrowing the old
// generated async table already used — plus two `if constexpr` arms that name a
// canonical converter instead. Adding a type to the Qt surface adds nothing
// here. If a conversion is missing it gets added to logos_json_convert, never
// inlined into a generator.
//
// Kept separate from logos_qt_lp_bridge.h (the transport seam) so it can be
// exercised on its own, without a client or a running module.
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "logos_codec.h"          // the canonical LIDL codec (logos-protocol)
#include "logos_json_convert.h"   // the canonical converters (logos-protocol)
#include "logos_types.h"          // LogosResult

namespace logos {
namespace qt {

// Qt surface value -> canonical JSON. One canonical call, every type.
inline nlohmann::json toWire(const QVariant& v)
{
    return logos::qvariantToNlohmann(v);
}

// Canonical JSON -> a Qt surface value of static type T.
//
//   * LogosResult — the canonical inverse of the LogosResult branch inside
//     qvariantToNlohmann. Named explicitly because `qvariant_cast<LogosResult>`
//     of the decoded QVariantMap would silently default-construct
//     (success=false) instead of failing.
//   * QVariant (`any`) — the decoded value IS the surface value; narrowing it
//     would be wrong, not merely redundant.
//   * everything else — one uniform narrowing, no per-type table.
template <class T>
T fromWire(const nlohmann::json& j)
{
    if constexpr (std::is_same_v<T, LogosResult>) {
        return logos::jsonToLogosResult(j);
    } else if constexpr (std::is_same_v<T, QVariant>) {
        return logos::nlohmannToQVariant(j);
    } else {
        return qvariant_cast<T>(logos::nlohmannToQVariant(j));
    }
}

// ── strict decode, for the generator's ELEMENT loops ───────────────────────
//
// `fromWire<T>` above is LENIENT by construction: it ends in `qvariant_cast`,
// Qt's uniform narrowing, which answers 0 for `qvariant_cast<qulonglong>` of
// the string "x". On a scalar slot that leniency is the shipped contract and is
// left exactly as it is. On an ELEMENT it is indefensible: `["x", 5]` read as
// `[uint]` reached the caller as [0, 5] — a value the provider never sent, with
// no warning anywhere, while every std consumer of the same contract got the
// codec's "expected integer at [0], got string".
//
// So a typed container decodes its elements through THE CODEC — the same
// logos::fromJson<T> the cdylib dispatch, the Rust provider and the plain wire
// use — and reports the mismatch instead of coercing it. Nothing about what
// counts as a legal value is decided here; a whole-valued 3.0 is a legal
// integer and 3.7 is not because Codec says so, and `bstr` keeps its documented
// lenient form for the same reason.
//
// `out` is untouched when this returns false, so a caller that ignores the
// result gets a default rather than a corrupted value.
//
// `any` (QVariant) and `result` cannot fail: `any` declares nothing to check
// against, and jsonToLogosResult is total.
template <class T>
bool tryFromWire(const nlohmann::json& j, T& out, std::string* reason = nullptr)
{
    try {
        if constexpr (std::is_same_v<T, QVariant>) {
            out = logos::nlohmannToQVariant(j);
        } else if constexpr (std::is_same_v<T, LogosResult>) {
            out = logos::jsonToLogosResult(j);
        } else if constexpr (std::is_same_v<T, QString>) {
            out = QString::fromStdString(logos::fromJson<std::string>(j, std::string()));
        } else if constexpr (std::is_same_v<T, QByteArray>) {
            const std::vector<uint8_t> b =
                logos::fromJson<std::vector<uint8_t>>(j, std::string());
            out = QByteArray(reinterpret_cast<const char*>(b.data()),
                             static_cast<qsizetype>(b.size()));
        } else if constexpr (std::is_same_v<T, QStringList>) {
            const std::vector<std::string> v =
                logos::fromJson<std::vector<std::string>>(j, std::string());
            QStringList acc;
            acc.reserve(static_cast<qsizetype>(v.size()));
            for (const std::string& e : v) acc.append(QString::fromStdString(e));
            out = acc;
        } else if constexpr (std::is_same_v<T, QVariantList>) {
            // Shape only — `[any]`'s elements declare nothing. Same rule the
            // provider-side decoder applies to the identical spelling.
            logos::fromJson<std::vector<nlohmann::json>>(j, std::string());
            out = logos::nlohmannToQVariant(j).toList();
        } else if constexpr (std::is_same_v<T, QVariantMap>) {
            logos::fromJson<std::map<std::string, nlohmann::json>>(j, std::string());
            out = logos::nlohmannToQVariant(j).toMap();
        } else if constexpr (std::is_arithmetic_v<T>) {
            out = logos::fromJson<T>(j, std::string());
        } else {
            // No codec rule for T — keep the lenient narrowing rather than
            // refusing a value that works today.
            out = fromWire<T>(j);
        }
        return true;
    } catch (const logos::CodecError& e) {
        // The codec's own diagnostic, verbatim — "expected integer at value,
        // got string". Re-wording it here would give one mismatch two
        // descriptions depending on which surface reported it.
        if (reason) *reason = e.what();
        return false;
    }
}

}  // namespace qt
}  // namespace logos

#endif  // LOGOS_QT_WIRE_H
