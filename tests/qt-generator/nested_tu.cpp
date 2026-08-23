// COMPILATION IS THE ASSERTION, and the round trips beside it are the second one.
//
// roundtrip_tu.cpp covers one slot per ROW of the widened Qt type table. This
// file covers the other axis — one slot per two-container COMBINATION — because
// that axis is where the emitter had shapes nothing had ever generated:
//
//   {tstr: {tstr: Point}}  emitted `for (auto __i = __i.value().cbegin(); ...)`,
//                          `__i` used inside its own initialiser;
//   ?{tstr: Point}         emitted `*__c.cbegin()`, which parses as
//                          `*(__c.cbegin())` — a std::optional asked for an
//                          iterator.
//
// Neither is a wrong VALUE, so no round trip could have caught them and no
// golden diff would have shown them: they are text that does not compile. The
// fixture beside this file enumerates all nine C1(C2(L)) pairs for a checked
// leaf and for a record, the leaves whose Qt spelling is special (QStringList,
// bstr, `any`), and a sample at depth three. `#include`ing the generated .cpp
// is what turns "the generator emitted something" into "a compiler accepted it"
// — and it is also what makes the file-local record codecs reachable, so the
// same expressions can then be RUN.

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include "nested_module_api.cpp"

using Point = NestedModule::Point;
using UintNest = NestedModule::UintNest;
using RecNest = NestedModule::RecNest;
using LeafNest = NestedModule::LeafNest;
using DeepNest = NestedModule::DeepNest;
using Everything = NestedModule::Everything;

// A record-bearing shape has no operator== to compare with — the contract never
// asked for one, so the generated struct has none, and QList/QMap/std::optional
// are all SFINAE-constrained on their element having one. So a record round trip
// is compared by RE-ENCODING both sides:
//
//     encode(decode(encode(v)))  ==  encode(v)
//
// which is the round-trip property stated exactly, over the whole record at
// once, and needs nothing the contract did not declare. It is checked against
// explicit wire-SHAPE assertions elsewhere in this file, so a symmetric
// encode/decode bug cannot satisfy both.
#define EXPECT_RECORD_ROUNDTRIP(enc, dec, value)                       \
    do {                                                               \
        const nlohmann::json __j = enc(value);                         \
        EXPECT_EQ(enc(dec(__j)), __j) << __j.dump();                   \
    } while (0)

namespace {

const Point kP1{ 1.5, -2.5 };
const Point kP2{ 3.0, 4.0 };

UintNest makeUintNest()
{
    UintNest n;
    // 2^64-1 in every position: the value a signed spelling re-signs, checked
    // at depth rather than only at the top level.
    const qulonglong big = 18446744073709551615ULL;
    n.ll = { { 1ULL, big }, {} };
    n.lm = { QMap<QString, qulonglong>{ { QStringLiteral("a"), big } } };
    n.lo = { std::optional<qulonglong>(big), std::nullopt };
    n.ml["k"] = { 7ULL, big };
    n.mm["k"] = QMap<QString, qulonglong>{ { QStringLiteral("i"), big } };
    n.mo["k"] = big;
    n.mo["z"] = std::nullopt;
    n.ol = QList<qulonglong>{ big };
    n.om = QMap<QString, qulonglong>{ { QStringLiteral("k"), big } };
    n.oo = big;
    return n;
}

RecNest makeRecNest()
{
    RecNest n;
    n.ll = { { kP1, kP2 }, {} };
    n.lm = { QMap<QString, Point>{ { QStringLiteral("a"), kP1 } } };
    n.lo = { std::optional<Point>(kP1), std::nullopt };
    n.ml["k"] = { kP1, kP2 };
    n.mm["k"] = QMap<QString, Point>{ { QStringLiteral("i"), kP2 } };
    n.mo["k"] = kP1;
    n.mo["z"] = std::nullopt;
    n.ol = QList<Point>{ kP2 };
    n.om = QMap<QString, Point>{ { QStringLiteral("k"), kP1 } };
    n.oo = kP2;
    return n;
}

LeafNest makeLeafNest()
{
    LeafNest n;
    const QByteArray blob("\x00\x01\xff\x7f\x80", 5);
    n.ss_ll = { QStringList{ QStringLiteral("a"), QStringLiteral("b") }, QStringList{} };
    n.ss_ml["k"] = QStringList{ QStringLiteral("v") };
    n.ss_ol = QStringList{ QStringLiteral("only") };
    n.ss_lo = { std::optional<QString>(QStringLiteral("here")), std::nullopt };
    n.ss_mm["k"] = QMap<QString, QString>{ { QStringLiteral("i"), QStringLiteral("v") } };
    n.bb_ll = { { blob, QByteArray() } };
    n.bb_ml["k"] = { blob };
    n.bb_ol = QList<QByteArray>{ blob };
    n.bb_lo = { std::optional<QByteArray>(blob), std::nullopt };
    n.aa_ll = QVariantList{ QVariantList{ QStringLiteral("x"), 5 } };
    n.aa_ml["k"] = QVariantList{ 1, true };
    n.aa_ol = QVariant(QVariantList{ QStringLiteral("y") });
    n.aa_lo = QVariantList{ QStringLiteral("z"), QVariant() };
    n.aa_mm["k"] = QVariantMap{ { QStringLiteral("i"), QStringLiteral("v") } };
    return n;
}

DeepNest makeDeepNest()
{
    DeepNest n;
    n.lll = { { { 1ULL, 2ULL }, {} }, {} };
    n.mmm["a"] = QMap<QString, QMap<QString, qulonglong>>{
        { QStringLiteral("b"), QMap<QString, qulonglong>{ { QStringLiteral("c"), 3ULL } } }
    };
    n.olm = QList<QMap<QString, Point>>{ QMap<QString, Point>{ { QStringLiteral("p"), kP1 } } };
    n.lom = { std::optional<QMap<QString, qulonglong>>(
                  QMap<QString, qulonglong>{ { QStringLiteral("k"), 9ULL } }),
              std::nullopt };
    n.mol["k"] = QList<Point>{ kP2 };
    n.mol["z"] = std::nullopt;
    n.lmo = { QMap<QString, std::optional<QString>>{
        { QStringLiteral("a"), std::optional<QString>(QStringLiteral("v")) },
        { QStringLiteral("b"), std::nullopt } } };
    n.oml = QMap<QString, QList<Point>>{ { QStringLiteral("k"), QList<Point>{ kP1 } } };
    n.mlo["k"] = { std::optional<Point>(kP2), std::nullopt };
    return n;
}

Everything makeEverything()
{
    Everything e;
    e.uints = makeUintNest();
    e.recs = makeRecNest();
    e.leaves = makeLeafNest();
    e.deep = makeDeepNest();
    e.bag_of_recs["k"] = QList<RecNest>{ makeRecNest() };
    return e;
}

}  // namespace

// ── the two shapes that did not compile ─────────────────────────────────────
//
// Stated on their own, ahead of the exhaustive round trip, so the regression
// they guard fails with its own name rather than as one line of a long test.
// The BUILD is the real assertion for them; these run the result.

TEST(NestedShapes, MapOfMapOfRecordRoundTrips)
{
    RecNest in;
    in.mm["outer"] = QMap<QString, Point>{ { QStringLiteral("inner"), kP1 } };

    const nlohmann::json j = recToWire_NestedModule_RecNest(in);
    ASSERT_TRUE(j.at("mm").is_object()) << j.at("mm").dump();
    ASSERT_TRUE(j.at("mm").at("outer").is_object());
    EXPECT_DOUBLE_EQ(j.at("mm").at("outer").at("inner").at("x").get<double>(), 1.5);

    const RecNest out = recFromWire_NestedModule_RecNest(j);
    ASSERT_TRUE(out.mm.contains(QStringLiteral("outer")));
    ASSERT_TRUE(out.mm.value(QStringLiteral("outer")).contains(QStringLiteral("inner")));
    EXPECT_DOUBLE_EQ(out.mm.value(QStringLiteral("outer")).value(QStringLiteral("inner")).y, -2.5);
}

TEST(NestedShapes, OptionalMapOfRecordRoundTrips)
{
    RecNest in;
    in.om = QMap<QString, Point>{ { QStringLiteral("k"), kP2 } };

    const nlohmann::json j = recToWire_NestedModule_RecNest(in);
    ASSERT_TRUE(j.at("om").is_object()) << j.at("om").dump();
    EXPECT_DOUBLE_EQ(j.at("om").at("k").at("y").get<double>(), 4.0);

    const RecNest out = recFromWire_NestedModule_RecNest(j);
    ASSERT_TRUE(out.om.has_value());
    EXPECT_DOUBLE_EQ(out.om->value(QStringLiteral("k")).x, 3.0);

    // And the EMPTY optional still omits its key and comes back empty.
    const RecNest empty = recFromWire_NestedModule_RecNest(recToWire_NestedModule_RecNest(RecNest{}));
    EXPECT_FALSE(empty.om.has_value());
}

// ── the enumeration ─────────────────────────────────────────────────────────

TEST(NestedShapes, EveryTwoContainerPairSurvivesTheWireForACheckedLeaf)
{
    const UintNest in = makeUintNest();
    const UintNest out = recFromWire_NestedModule_UintNest(recToWire_NestedModule_UintNest(in));

    EXPECT_EQ(out.ll, in.ll);
    EXPECT_EQ(out.lm, in.lm);
    EXPECT_EQ(out.lo, in.lo);
    EXPECT_EQ(out.ml, in.ml);
    EXPECT_EQ(out.mm, in.mm);
    EXPECT_EQ(out.mo, in.mo);
    EXPECT_EQ(out.ol, in.ol);
    EXPECT_EQ(out.om, in.om);
    EXPECT_EQ(out.oo, in.oo);
}

TEST(NestedShapes, EveryTwoContainerPairSurvivesTheWireForARecord)
{
    const RecNest in = makeRecNest();
    EXPECT_RECORD_ROUNDTRIP(recToWire_NestedModule_RecNest, recFromWire_NestedModule_RecNest, in);

    // And the wire shape of each pair, so the round trip above cannot be
    // satisfied by two containers that are both empty.
    const nlohmann::json j = recToWire_NestedModule_RecNest(in);
    EXPECT_TRUE(j.at("ll").at(0).is_array());
    EXPECT_TRUE(j.at("lm").at(0).is_object());
    EXPECT_TRUE(j.at("lo").at(0).is_object());
    EXPECT_TRUE(j.at("lo").at(1).is_null());
    EXPECT_TRUE(j.at("ml").at("k").is_array());
    EXPECT_TRUE(j.at("mm").at("k").is_object());
    EXPECT_TRUE(j.at("mo").at("k").is_object());
    EXPECT_TRUE(j.at("mo").at("z").is_null());
    EXPECT_TRUE(j.at("ol").is_array());
    EXPECT_TRUE(j.at("om").is_object());
    EXPECT_TRUE(j.at("oo").is_object());

    // Two values read back through the struct, so the decode is not merely
    // symmetric with the encode.
    const RecNest out = recFromWire_NestedModule_RecNest(j);
    ASSERT_EQ(out.ll.size(), 2);
    ASSERT_EQ(out.ll.at(0).size(), 2);
    EXPECT_DOUBLE_EQ(out.ll.at(0).at(1).y, 4.0);
    ASSERT_TRUE(out.oo.has_value());
    EXPECT_DOUBLE_EQ(out.oo->x, 3.0);
}

TEST(NestedShapes, TheSpeciallySpelledLeavesSurviveAtDepth)
{
    const LeafNest in = makeLeafNest();
    const nlohmann::json j = recToWire_NestedModule_LeafNest(in);

    // QStringList crosses WHOLE, so the outer container loops and the inner
    // value arrives intact — still a JSON array of strings, not a null.
    ASSERT_TRUE(j.at("ss_ll").is_array()) << j.at("ss_ll").dump();
    EXPECT_TRUE(j.at("ss_ll").at(0).is_array());
    EXPECT_EQ(j.at("ss_ll").at(0).at(0).get<std::string>(), "a");

    // Bytes stay TAGGED however deep they sit.
    EXPECT_TRUE(j.at("bb_ll").at(0).at(0).contains("_bytes"));
    EXPECT_TRUE(j.at("bb_ml").at("k").at(0).contains("_bytes"));
    EXPECT_TRUE(j.at("bb_ol").at(0).contains("_bytes"));
    EXPECT_TRUE(j.at("bb_lo").at(0).contains("_bytes"));

    const LeafNest out = recFromWire_NestedModule_LeafNest(j);
    EXPECT_EQ(out.ss_ll, in.ss_ll);
    EXPECT_EQ(out.ss_ml, in.ss_ml);
    EXPECT_EQ(out.ss_ol, in.ss_ol);
    EXPECT_EQ(out.ss_lo, in.ss_lo);
    EXPECT_EQ(out.ss_mm, in.ss_mm);
    EXPECT_EQ(out.bb_ll, in.bb_ll);
    EXPECT_EQ(out.bb_ml, in.bb_ml);
    EXPECT_EQ(out.bb_ol, in.bb_ol);
    EXPECT_EQ(out.bb_lo, in.bb_lo);
    // `any` is untyped at every depth, so a mixed payload is legal and arrives
    // as the QVariant family — the rows that must NOT widen.
    EXPECT_EQ(out.aa_ll, in.aa_ll);
    EXPECT_EQ(out.aa_ml, in.aa_ml);
    EXPECT_EQ(out.aa_mm, in.aa_mm);
}

TEST(NestedShapes, DepthThreeSurvivesTheWire)
{
    const DeepNest in = makeDeepNest();
    const nlohmann::json j = recToWire_NestedModule_DeepNest(in);
    ASSERT_TRUE(j.at("lll").at(0).at(0).is_array()) << j.at("lll").dump();
    ASSERT_TRUE(j.at("mmm").at("a").at("b").is_object());

    const DeepNest out = recFromWire_NestedModule_DeepNest(j);
    // The record-free shapes compare directly; the record-bearing ones go
    // through the re-encode, for the reason above the macro.
    EXPECT_EQ(out.lll, in.lll);
    EXPECT_EQ(out.mmm, in.mmm);
    EXPECT_EQ(out.lom, in.lom);
    EXPECT_EQ(out.lmo, in.lmo);
    EXPECT_RECORD_ROUNDTRIP(recToWire_NestedModule_DeepNest, recFromWire_NestedModule_DeepNest, in);

    ASSERT_TRUE(out.olm.has_value());
    EXPECT_DOUBLE_EQ(out.olm->at(0).value(QStringLiteral("p")).x, 1.5);
    ASSERT_TRUE(out.mol.value(QStringLiteral("k")).has_value());
    EXPECT_DOUBLE_EQ(out.mol.value(QStringLiteral("k"))->at(0).y, 4.0);
    EXPECT_FALSE(out.mol.value(QStringLiteral("z")).has_value());
    ASSERT_TRUE(out.oml.has_value());
    EXPECT_DOUBLE_EQ(out.oml->value(QStringLiteral("k")).at(0).x, 1.5);
    ASSERT_EQ(out.mlo.value(QStringLiteral("k")).size(), 2);
    EXPECT_TRUE(out.mlo.value(QStringLiteral("k")).at(0).has_value());
    EXPECT_FALSE(out.mlo.value(QStringLiteral("k")).at(1).has_value());
}

TEST(NestedShapes, ARecordOfRecordsSurvivesThroughAContainer)
{
    const Everything in = makeEverything();
    EXPECT_RECORD_ROUNDTRIP(recToWire_NestedModule_Everything, recFromWire_NestedModule_Everything, in);

    const Everything out = recFromWire_NestedModule_Everything(recToWire_NestedModule_Everything(in));
    EXPECT_EQ(out.uints.mm, in.uints.mm);
    EXPECT_EQ(out.deep.lll, in.deep.lll);
    ASSERT_TRUE(out.recs.om.has_value());
    EXPECT_DOUBLE_EQ(out.recs.om->value(QStringLiteral("k")).x, 1.5);
    // A record reached through a container of records, two levels of struct
    // down: `{tstr: [RecNest]}` -> RecNest -> `[[Point]]`.
    ASSERT_TRUE(out.bag_of_recs.contains(QStringLiteral("k")));
    ASSERT_EQ(out.bag_of_recs.value(QStringLiteral("k")).size(), 1);
    EXPECT_DOUBLE_EQ(out.bag_of_recs.value(QStringLiteral("k")).at(0).ll.at(0).at(1).y, 4.0);
}

// ── the error channel (D5) ──────────────────────────────────────────────────
//
// A rejected element used to leave an empty container and a qWarning, and
// nothing a caller could branch on: `err.ok()` was true. These assert the sink
// the generated loops now write into — the same `std::string*` the method
// bodies fold into `logos::CallError` — at each of the depths a rejection can
// happen at.

TEST(NestedDecodeErrors, ABadElementAtDepthTwoIsReported)
{
    nlohmann::json j = nlohmann::json::object();
    j["ll"] = nlohmann::json::array({ nlohmann::json::array({ 1, "x" }) });

    std::string why;
    const UintNest out = recFromWire_NestedModule_UintNest(j, &why);

    ASSERT_EQ(out.ll.size(), 1);
    EXPECT_TRUE(out.ll.at(0).isEmpty()) << "the bad element must refuse the container";
    EXPECT_FALSE(why.empty()) << "an empty sink is the silent-empty-with-ok defect";
    EXPECT_NE(why.find("uint"), std::string::npos) << why;
    EXPECT_NE(why.find("got string"), std::string::npos) << why;
}

TEST(NestedDecodeErrors, ABadElementAtDepthThreeIsReported)
{
    nlohmann::json j = nlohmann::json::object();
    j["lll"] = nlohmann::json::array(
        { nlohmann::json::array({ nlohmann::json::array({ 1, "x" }) }) });

    std::string why;
    const DeepNest out = recFromWire_NestedModule_DeepNest(j, &why);

    ASSERT_EQ(out.lll.size(), 1);
    ASSERT_EQ(out.lll.at(0).size(), 1);
    EXPECT_TRUE(out.lll.at(0).at(0).isEmpty());
    EXPECT_FALSE(why.empty()) << "depth three is still one loop, and must still report";
    EXPECT_NE(why.find("got string"), std::string::npos) << why;
}

TEST(NestedDecodeErrors, AWrongShapedContainerIsReportedNotSilentlyEmpty)
{
    // `[[uint]]` handed an object, and `{tstr: uint}` handed a string: the
    // SHAPE is as much part of the declared type as the element type is, and
    // answering an empty container for either is the same silent lie.
    {
        nlohmann::json j = nlohmann::json::object();
        j["ll"] = nlohmann::json::object();
        std::string why;
        const UintNest out = recFromWire_NestedModule_UintNest(j, &why);
        EXPECT_TRUE(out.ll.isEmpty());
        EXPECT_NE(why.find("expected array"), std::string::npos) << why;
    }
    {
        nlohmann::json j = nlohmann::json::object();
        j["ml"] = nlohmann::json::array({ 1 });
        std::string why;
        const UintNest out = recFromWire_NestedModule_UintNest(j, &why);
        EXPECT_TRUE(out.ml.isEmpty());
        EXPECT_NE(why.find("expected object"), std::string::npos) << why;
    }
    {
        // The wrong shape one level IN: `[[uint]]` whose element is an object.
        nlohmann::json j = nlohmann::json::object();
        j["ll"] = nlohmann::json::array({ nlohmann::json::object() });
        std::string why;
        recFromWire_NestedModule_UintNest(j, &why);
        EXPECT_NE(why.find("expected array"), std::string::npos) << why;
    }
}

TEST(NestedDecodeErrors, TheFirstRejectionWins)
{
    nlohmann::json j = nlohmann::json::object();
    j["ll"] = nlohmann::json::array({ nlohmann::json::array({ "first" }) });
    j["ml"] = nlohmann::json::object({ { "k", nlohmann::json::array({ "second" }) } });

    std::string why;
    recFromWire_NestedModule_UintNest(j, &why);
    ASSERT_FALSE(why.empty());
    // Both slots reject; the sink keeps the one that names the actual mismatch
    // a reader will chase, not the last one the walk happened to reach.
    EXPECT_EQ(why.find("`[[uint]]`"), std::string::npos) << why;
    EXPECT_NE(why.find("`uint`"), std::string::npos) << why;
}

TEST(NestedDecodeErrors, ANullSinkIsTheSurfaceWithNoErrorChannel)
{
    // Events and the value-only async callback have nowhere to put an error, so
    // they pass a null sink. That must be a no-op, not a crash — and the value
    // must still be refused rather than coerced.
    nlohmann::json j = nlohmann::json::object();
    j["ll"] = nlohmann::json::array({ nlohmann::json::array({ "x" }) });

    const UintNest out = recFromWire_NestedModule_UintNest(j, nullptr);
    EXPECT_TRUE(out.ll.at(0).isEmpty());
}

TEST(NestedDecodeErrors, AGoodValueLeavesTheSinkUntouched)
{
    // The control. Without it every assertion above is satisfied by a sink that
    // is filled unconditionally.
    std::string why;
    const Everything out = recFromWire_NestedModule_Everything(recToWire_NestedModule_Everything(makeEverything()), &why);
    EXPECT_TRUE(why.empty()) << why;
    EXPECT_EQ(out.uints.oo, makeEverything().uints.oo);
    EXPECT_EQ(out.deep.mmm, makeEverything().deep.mmm);
}
