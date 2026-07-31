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

#include <QVariant>

#include <type_traits>

#include <nlohmann/json.hpp>

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

}  // namespace qt
}  // namespace logos

#endif  // LOGOS_QT_WIRE_H
