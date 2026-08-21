// RUNTIME round-trip through the code the generator actually emits.
//
// Every other test beside this one reads the generator's TEXT. That is not
// enough for the type mapping, because its failure mode is not a compile error
// and not a diff — it is a VALUE that silently changes on the way through.
// `QVariant::fromValue(QList<qulonglong>)` compiles, produces a QVariant, and
// serialises to JSON **null**, because logos::qvariantToNlohmann matches a
// closed `userType()` set that QList<qulonglong> is not in. Coming back,
// `qvariant_cast<QList<qulonglong>>` of a QVariantList compiles and yields an
// EMPTY list. A generated wrapper built that way passes every text assertion
// and loses the payload of every typed call.
//
// So this file `#include`s the generated .cpp — the same trick
// fixtures/umbrella_tu.cpp uses, which is what makes the file-local record
// codecs (`recToWire_Bag` / `recFromWire_Bag`) reachable — and pushes real
// values through them. Those two functions are built from EXACTLY the same
// `toWire` / `fromWire` expressions the generator emits for a method argument
// and a return value; there is one emitter and one table, so a value that
// survives a Bag field survives an argument.
//
// The .cpp brings its method bodies with it, so this TU also has to LINK
// against the bridge those bodies call. That is a second assertion for free:
// the widened wrappers are not merely compilable, they resolve.

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include "widened_module_api.cpp"

using Bag = WidenedModule::Bag;
using Point = WidenedModule::Point;

namespace {

// The value under test, filled at the BOUNDARIES rather than with pretty
// numbers: 2^64-1 is the value a signed spelling silently re-signs, and a byte
// string with an embedded NUL and a high byte is the one a JSON string cannot
// carry.
Bag makeBag()
{
    Bag b;
    b.uints = { 0ULL, 1ULL, 18446744073709551615ULL };
    b.ints  = { -9223372036854775807LL - 1, -1, 9223372036854775807LL };
    b.blobs = { QByteArray("\x00\x01\xff\x7f\x80", 5), QByteArray(), QByteArray("hi") };
    b.flags = { true, false, true };
    b.reals = { -0.5, 0.0, 3.25 };
    b.names = { QStringLiteral("a"), QStringLiteral("b") };
    b.loose = QVariantList{ QStringLiteral("x"), 5, true };
    b.counts["k"] = 18446744073709551615ULL;
    b.counts["z"] = 0ULL;
    b.labels["k"] = QStringLiteral("v");
    b.chunks["k"] = QByteArray("\x00\xfe", 2);
    b.attrs["k"] = QStringLiteral("v");
    b.maybe_text = QStringLiteral("present");
    b.maybe_uint = 18446744073709551615ULL;
    b.maybe_blob = QByteArray("\x00\x01", 2);
    b.maybe_any = QVariant(QStringLiteral("any"));
    b.grid = { { 1ULL, 2ULL }, {}, { 18446744073709551615ULL } };
    b.buckets["k"] = QList<qulonglong>{ 7ULL };
    b.rows = { QMap<QString, qulonglong>{ { QStringLiteral("a"), 1ULL } } };
    b.holes = { std::optional<QString>(QStringLiteral("here")), std::nullopt };
    b.origin = Point{ 1.5, -2.5 };
    b.corners = { Point{ 0.0, 0.0 }, Point{ 3.0, 4.0 } };
    b.named_points["p"] = Point{ 9.0, 8.0 };
    b.maybe_point = Point{ 6.0, 7.0 };
    return b;
}

}  // namespace

// ── the round trip ──────────────────────────────────────────────────────────

TEST(WidenedRoundTrip, EveryWidenedFieldSurvivesTheWire)
{
    const Bag in = makeBag();
    const Bag out = recFromWire_Bag(recToWire_Bag(in));

    EXPECT_EQ(out.uints, in.uints);
    EXPECT_EQ(out.ints, in.ints);
    EXPECT_EQ(out.blobs, in.blobs);
    EXPECT_EQ(out.flags, in.flags);
    EXPECT_EQ(out.reals, in.reals);
    EXPECT_EQ(out.names, in.names);
    EXPECT_EQ(out.loose, in.loose);
    EXPECT_EQ(out.counts, in.counts);
    EXPECT_EQ(out.labels, in.labels);
    EXPECT_EQ(out.chunks, in.chunks);
    EXPECT_EQ(out.attrs, in.attrs);
    EXPECT_EQ(out.maybe_text, in.maybe_text);
    EXPECT_EQ(out.maybe_uint, in.maybe_uint);
    EXPECT_EQ(out.maybe_blob, in.maybe_blob);
    EXPECT_EQ(out.maybe_any, in.maybe_any);
    EXPECT_EQ(out.grid, in.grid);
    EXPECT_EQ(out.buckets, in.buckets);
    EXPECT_EQ(out.rows, in.rows);
    EXPECT_EQ(out.holes, in.holes);
    EXPECT_EQ(out.corners.size(), in.corners.size());
    EXPECT_DOUBLE_EQ(out.origin.x, in.origin.x);
    EXPECT_DOUBLE_EQ(out.named_points["p"].y, 8.0);
    ASSERT_TRUE(out.maybe_point.has_value());
    EXPECT_DOUBLE_EQ(out.maybe_point->x, 6.0);
}

// 2^64-1 is the assertion the OLD spelling could not make. A QVariantList of
// qulonglong round-tripped it fine; what could not was any surface that read
// the element back as signed. Stated on its own so it fails with its own name.
TEST(WidenedRoundTrip, UintKeepsItsTopBit)
{
    Bag in;
    in.uints = { 18446744073709551615ULL };
    in.counts["k"] = 18446744073709551615ULL;
    in.maybe_uint = 18446744073709551615ULL;

    const nlohmann::json j = recToWire_Bag(in);
    // On the WIRE it must be an unsigned integer, not a double and not -1.
    ASSERT_TRUE(j.at("uints").at(0).is_number_unsigned());
    EXPECT_EQ(j.at("uints").at(0).get<uint64_t>(), 18446744073709551615ULL);

    const Bag out = recFromWire_Bag(j);
    EXPECT_EQ(out.uints.at(0), 18446744073709551615ULL);
    EXPECT_EQ(out.counts.value("k"), 18446744073709551615ULL);
    ASSERT_TRUE(out.maybe_uint.has_value());
    EXPECT_EQ(*out.maybe_uint, 18446744073709551615ULL);
}

// Bytes are TAGGED at every depth — the canonical {"_bytes": base64url} form —
// which is the only shape a JSON string cannot mangle. An embedded NUL and a
// 0x80 byte are exactly what a plain-string encoding loses.
TEST(WidenedRoundTrip, BytesStayTaggedInsideTypedContainers)
{
    Bag in;
    in.blobs = { QByteArray("\x00\x01\xff\x7f\x80", 5) };
    in.chunks["k"] = QByteArray("\x00\xfe", 2);
    in.maybe_blob = QByteArray("\x00", 1);

    const nlohmann::json j = recToWire_Bag(in);
    ASSERT_TRUE(j.at("blobs").at(0).is_object());
    EXPECT_TRUE(j.at("blobs").at(0).contains("_bytes"));
    EXPECT_TRUE(j.at("chunks").at("k").contains("_bytes"));
    EXPECT_TRUE(j.at("maybe_blob").contains("_bytes"));

    const Bag out = recFromWire_Bag(j);
    EXPECT_EQ(out.blobs.at(0), in.blobs.at(0));
    EXPECT_EQ(out.blobs.at(0).size(), 5);
    EXPECT_EQ(out.chunks.value("k"), in.chunks.value("k"));
    ASSERT_TRUE(out.maybe_blob.has_value());
    EXPECT_EQ(*out.maybe_blob, in.maybe_blob.value());
}

// THE ANTI-REGRESSION FOR THE TRAP. A widened container must never reach
// toWire as a whole value: that produces JSON `null`, not an array. Asserting
// the WIRE SHAPE, because a round-trip alone would still pass if both
// directions were broken symmetrically (null out, default in).
TEST(WidenedRoundTrip, TypedContainersEncodeAsArraysAndObjectsNotNull)
{
    const Bag in = makeBag();
    const nlohmann::json j = recToWire_Bag(in);

    EXPECT_TRUE(j.at("uints").is_array())   << j.at("uints").dump();
    EXPECT_TRUE(j.at("ints").is_array());
    EXPECT_TRUE(j.at("blobs").is_array());
    EXPECT_TRUE(j.at("flags").is_array());
    EXPECT_TRUE(j.at("reals").is_array());
    EXPECT_TRUE(j.at("grid").is_array());
    EXPECT_TRUE(j.at("grid").at(0).is_array());
    EXPECT_TRUE(j.at("holes").is_array());
    EXPECT_TRUE(j.at("counts").is_object()) << j.at("counts").dump();
    EXPECT_TRUE(j.at("labels").is_object());
    EXPECT_TRUE(j.at("chunks").is_object());
    EXPECT_TRUE(j.at("buckets").is_object());
    EXPECT_TRUE(j.at("buckets").at("k").is_array());
    EXPECT_EQ(j.at("uints").size(), 3u);
    EXPECT_EQ(j.at("counts").size(), 2u);
}

// ── optionality ─────────────────────────────────────────────────────────────

// An EMPTY optional omits its key; an ABSENT key decodes back to empty. Both
// spellings — the `? name:` flag and the `name: ?T` type — must behave
// identically, which is why the fixture uses one of each.
TEST(WidenedRoundTrip, EmptyOptionalOmitsItsKeyAndComesBackEmpty)
{
    Bag in;   // every optional default-constructed, i.e. nullopt
    ASSERT_FALSE(in.maybe_text.has_value());
    ASSERT_FALSE(in.maybe_uint.has_value());

    const nlohmann::json j = recToWire_Bag(in);
    EXPECT_FALSE(j.contains("maybe_text"));   // flag spelling
    EXPECT_FALSE(j.contains("maybe_uint"));   // type spelling
    EXPECT_FALSE(j.contains("maybe_blob"));
    EXPECT_FALSE(j.contains("maybe_point"));

    const Bag out = recFromWire_Bag(j);
    EXPECT_FALSE(out.maybe_text.has_value());
    EXPECT_FALSE(out.maybe_uint.has_value());
    EXPECT_FALSE(out.maybe_blob.has_value());
    EXPECT_FALSE(out.maybe_point.has_value());
}

// An explicit JSON null means the same thing as an absent key — the two-state
// rule. A provider written in Rust or std C++ sends null; a C++ or Qt one omits
// the key. Both must arrive as empty.
TEST(WidenedRoundTrip, ExplicitNullDecodesToEmpty)
{
    nlohmann::json j = recToWire_Bag(makeBag());
    j["maybe_text"] = nullptr;
    j["maybe_uint"] = nullptr;
    j["maybe_blob"] = nullptr;
    j["maybe_point"] = nullptr;

    const Bag out = recFromWire_Bag(j);
    EXPECT_FALSE(out.maybe_text.has_value());
    EXPECT_FALSE(out.maybe_uint.has_value());
    EXPECT_FALSE(out.maybe_blob.has_value());
    EXPECT_FALSE(out.maybe_point.has_value());
}

// An empty `?tstr` is NOT the empty string, and a present empty string is not
// empty. This is the distinction the old QVariant spelling could technically
// make and the pre-optional bare-`QString` spelling could not.
TEST(WidenedRoundTrip, PresentEmptyStringIsNotAnEmptyOptional)
{
    Bag in;
    in.maybe_text = QString();          // present, and empty
    const nlohmann::json j = recToWire_Bag(in);
    ASSERT_TRUE(j.contains("maybe_text"));
    EXPECT_TRUE(j.at("maybe_text").is_string());

    const Bag out = recFromWire_Bag(j);
    ASSERT_TRUE(out.maybe_text.has_value());
    EXPECT_TRUE(out.maybe_text->isEmpty());
}

// A `[?tstr]` keeps its holes IN PLACE. A container that dropped an empty
// element would renumber everything after it.
TEST(WidenedRoundTrip, OptionalElementsKeepTheirPositions)
{
    Bag in;
    in.holes = { std::nullopt, std::optional<QString>(QStringLiteral("mid")), std::nullopt };

    const nlohmann::json j = recToWire_Bag(in);
    ASSERT_EQ(j.at("holes").size(), 3u);
    EXPECT_TRUE(j.at("holes").at(0).is_null());
    EXPECT_EQ(j.at("holes").at(1).get<std::string>(), "mid");

    const Bag out = recFromWire_Bag(j);
    ASSERT_EQ(out.holes.size(), 3);
    EXPECT_FALSE(out.holes.at(0).has_value());
    ASSERT_TRUE(out.holes.at(1).has_value());
    EXPECT_EQ(*out.holes.at(1), QStringLiteral("mid"));
    EXPECT_FALSE(out.holes.at(2).has_value());
}

// ── element type checking ───────────────────────────────────────────────────

// THE DEFECT THIS CHANGE EXISTS TO CLOSE, stated as a test.
//
// `["x", 5]` read as `[uint]` used to reach a Qt caller as [0, 5] — silently,
// because qvariant_cast answers 0 for a QString. Every std consumer of the same
// contract got the codec's "expected integer ..., got string". The typed
// decode now routes each element through that same codec, so the mismatch is
// REJECTED rather than coerced: the container comes back empty (and a qWarning
// carries the codec's own sentence), which is a refusal, not a value the
// provider never sent.
TEST(WidenedRoundTrip, AStringElementInAUintArrayIsRejectedNotZeroed)
{
    nlohmann::json j = nlohmann::json::object();
    j["uints"] = nlohmann::json::array({ "x", 5 });

    const Bag out = recFromWire_Bag(j);
    ASSERT_NE(out.uints, (QList<qulonglong>{ 0ULL, 5ULL }))
        << "a string element was silently coerced to 0 — the exact defect";
    EXPECT_TRUE(out.uints.isEmpty());
}

// ── the rejection reaches the ERROR CHANNEL, not only the log ───────────────
//
// The tests above assert that a bad element is REFUSED. That alone left the
// second half of the same defect open: the refusal produced an empty container
// and a qWarning, and a caller reading `err.ok()` saw SUCCESS — so a
// thousand-element list with one bad element and a list the provider
// legitimately sent empty were the same answer. The std consumer of the same
// contract THREW for that input; the two surfaces disagreed about whether a
// mistyped element is an error at all.
//
// `recFromWire_Bag`'s second parameter is that channel — the same `std::string*`
// the generated method bodies declare and fold into `logos::CallError` — so the
// rule can be stated here, on the emitted code, rather than only in generated
// text nothing runs.
TEST(WidenedRoundTrip, ARejectedElementReachesTheErrorChannel)
{
    nlohmann::json j = nlohmann::json::object();
    j["uints"] = nlohmann::json::array({ "x", 5 });

    std::string why;
    const Bag out = recFromWire_Bag(j, &why);

    EXPECT_TRUE(out.uints.isEmpty());
    ASSERT_FALSE(why.empty()) << "silent-empty-with-ok: the caller cannot tell a refusal "
                                 "from a list the provider really sent empty";
    // The CODEC's own sentence, carried through rather than re-worded — the
    // same text the std surface reports for this input.
    EXPECT_NE(why.find("expected integer"), std::string::npos) << why;
    EXPECT_NE(why.find("got string"), std::string::npos) << why;
    // And the slot, spelled the way the CONTRACT spells it.
    EXPECT_NE(why.find("`uint`"), std::string::npos) << why;
    EXPECT_NE(why.find("WidenedModule"), std::string::npos) << why;
}

TEST(WidenedRoundTrip, AWrongShapedResponseReachesTheErrorChannel)
{
    // Not a bad element — a bad SHAPE. `[uint]` handed an object and
    // `{tstr: uint}` handed an array both used to answer an empty container
    // with nothing on the error channel.
    {
        nlohmann::json j = nlohmann::json::object();
        j["uints"] = nlohmann::json::object();
        std::string why;
        const Bag out = recFromWire_Bag(j, &why);
        EXPECT_TRUE(out.uints.isEmpty());
        EXPECT_NE(why.find("expected array"), std::string::npos) << why;
    }
    {
        nlohmann::json j = nlohmann::json::object();
        j["counts"] = nlohmann::json::array({ 1 });
        std::string why;
        const Bag out = recFromWire_Bag(j, &why);
        EXPECT_TRUE(out.counts.isEmpty());
        EXPECT_NE(why.find("expected object"), std::string::npos) << why;
    }
}

TEST(WidenedRoundTrip, AGoodValueLeavesTheErrorChannelClean)
{
    // The control for the two above: without it, both are satisfied by a sink
    // that is filled on every decode.
    std::string why;
    const Bag out = recFromWire_Bag(recToWire_Bag(makeBag()), &why);
    EXPECT_TRUE(why.empty()) << why;
    EXPECT_EQ(out.uints, makeBag().uints);
}

// The same rule one level down, and through a map: the check is per ELEMENT at
// every depth, not a shape check on the outermost container.
TEST(WidenedRoundTrip, ElementCheckingReachesNestedContainers)
{
    nlohmann::json j = nlohmann::json::object();
    j["grid"] = nlohmann::json::array({ nlohmann::json::array({ 1, "x" }) });
    j["counts"] = { { "k", "not a number" } };
    j["buckets"] = { { "k", nlohmann::json::array({ "nope" }) } };

    const Bag out = recFromWire_Bag(j);
    ASSERT_EQ(out.grid.size(), 1);
    EXPECT_TRUE(out.grid.at(0).isEmpty());
    EXPECT_TRUE(out.counts.isEmpty());
    ASSERT_TRUE(out.buckets.contains("k"));
    EXPECT_TRUE(out.buckets.value("k").isEmpty());
}

// A whole-valued float IS a legal integer (JSON does not distinguish 3 from
// 3.0, and an argument-typing CLI produces 3.0 for "3"), while 3.7 is not.
// Neither rule is decided here — the codec owns both, and the point of routing
// elements through it is that the Qt surface inherits them rather than
// inventing a stricter or looser one.
TEST(WidenedRoundTrip, TheCodecOwnsWhatCountsAsAnInteger)
{
    nlohmann::json whole = nlohmann::json::object();
    whole["uints"] = nlohmann::json::array({ 3.0 });
    EXPECT_EQ(recFromWire_Bag(whole).uints, (QList<qulonglong>{ 3ULL }));

    nlohmann::json fractional = nlohmann::json::object();
    fractional["uints"] = nlohmann::json::array({ 3.7 });
    EXPECT_TRUE(recFromWire_Bag(fractional).uints.isEmpty());
}

// The untyped rows are untouched by all of the above, and must stay that way:
// `[any]` and `{tstr: any}` declare nothing about their elements, so there is
// nothing to check them against and a mixed payload is legal.
TEST(WidenedRoundTrip, AnyBottomedContainersStayUnchecked)
{
    nlohmann::json j = nlohmann::json::object();
    j["loose"] = nlohmann::json::array({ "x", 5, true });
    j["attrs"] = { { "s", "x" }, { "n", 5 } };

    const Bag out = recFromWire_Bag(j);
    ASSERT_EQ(out.loose.size(), 3);
    EXPECT_EQ(out.loose.at(0).toString(), QStringLiteral("x"));
    EXPECT_EQ(out.loose.at(1).toLongLong(), 5);
    EXPECT_EQ(out.attrs.value("s").toString(), QStringLiteral("x"));
}

// ── the _bytes collision (conformance xfail M3) ─────────────────────────────
//
// A one-key map {"_bytes": "hello"} is wire-identical to a canonical tagged byte
// string, and `isTaggedBytes` is tested BEFORE the object branch in
// logos_json_convert.cpp — so such a map reaching an UNTYPED Qt slot is
// reinterpreted as bytes and the map is gone.
//
// A TYPED map retires that for its own slot, and this is the test that says so:
// {tstr: tstr} decodes key by key, and the key/value decoders are never asked
// "is this whole object bytes?". The untyped `{tstr: any}` beside it still
// loses it — the ambiguity is inherent to the tagged form, and only a DECLARED
// value type can resolve it.
TEST(WidenedRoundTrip, TypedMapSurvivesTheBytesTagCollision)
{
    nlohmann::json j = nlohmann::json::object();
    j["labels"] = { { "_bytes", "hello" } };
    j["attrs"]  = { { "_bytes", "hello" } };

    const Bag out = recFromWire_Bag(j);

    // TYPED: still a map, with its key and its string value intact.
    ASSERT_TRUE(out.labels.contains(QStringLiteral("_bytes")))
        << "a typed {tstr: tstr} was reinterpreted as a byte string";
    EXPECT_EQ(out.labels.value(QStringLiteral("_bytes")), QStringLiteral("hello"));

    // UNTYPED, for contrast: `{tstr: any}` has no declared value type, so the
    // canonical converter's tagged-bytes rule still fires and the map is gone.
    // Documented, not endorsed — see conformance/known.json.
    EXPECT_TRUE(out.attrs.isEmpty());
}
