// Provider-side ARGUMENT decoding — registry entry Q1.
//
// A Qt-typed provider used to coerce an incoming argument into the declared
// parameter type before the author's method body ran, so a value the type
// cannot represent arrived as a plausible one and nothing signalled:
//
//     echoUint(-1)             ran the body with 18446744073709551615
//     echoInt(3.7)             ran it with 4
//     echoBool(1)              ran it with true
//     echoStringList(["a",1])  ran it with ["a","1"]
//     echoList("notalist")     ran it with the string split into characters
//     echoMap(5)               ran it with {}
//     stringLength(42)         ran it with "42"
//
// Every non-Qt provider (the generated cdylib dispatch, the Rust provider, the
// plain wire) decodes the same argument through logos::fromJson<T> and answers
// {"code":"dispatch_failed", ...}. These tests pin the surviving Qt provider
// site, and the converter every generated Qt dispatch is built from, to that
// same rule:
//
//   * logos::qtArgFromVariant<T> itself — the per-parameter converter a
//     generated Qt provider dispatch emits one call to per argument. Tested
//     directly rather than through a generated fixture: the LOGOS_METHOD
//     provider-header generator that used to build one was removed, and the
//     converter is the part of it that carries the rule;
//   * the QMetaObject dispatch (QtProviderObject::callMethod), which knows a
//     parameter only as a QMetaType and calls logos::qtArgDecode.
//
// The line between accepted and refused is the CODEC's, not one written here.
// The cases that document it are the whole-valued floats: 3.0 IS a legal
// integer (JSON does not distinguish 3 from 3.0, and an argument-typing CLI
// produces 3.0 for "3.0") while 3.7 is not. An earlier, stricter check broke
// four working cases; these tests exist so that lesson is not re-learned.
//
// The four typed-ARRAY cases of the nine are here rather than in the
// integration suite because the logoscore CLI in this workspace cannot express
// a JSON array argument at all — they would otherwise go unmeasured.

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "logos_api.h"
#include "logos_provider_object.h"
#include "logos_qt_arg_decode.h"
#include "qt_provider_object.h"
// By name, not by a path relative to this file — see the note in
// test_auth_token_enforcement.cpp.
#include <core/interface.h>

namespace {

// True for the canonical rejection object — the same {code,message,origin}
// shape a cdylib or Rust provider returns.
bool isDispatchFailed(const QVariant& v)
{
    if (v.userType() != QMetaType::QVariantMap) return false;
    return v.toMap().value(QStringLiteral("code")).toString()
        == QStringLiteral("dispatch_failed");
}

QString rejectionMessage(const QVariant& v)
{
    return v.toMap().value(QStringLiteral("message")).toString();
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. logos::qtArgFromVariant<T> — what the generated dispatch emits.
// ═══════════════════════════════════════════════════════════════════════════

class QtArgFromVariantTest : public ::testing::Test {};

// -- refusals -----------------------------------------------------------------

TEST_F(QtArgFromVariantTest, NegativeForUintIsRefused)
{
    // The headline case: -1 used to arrive as 18446744073709551615.
    EXPECT_THROW(logos::qtArgFromVariant<qulonglong>(QVariant(-1), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, FractionalFloatForIntIsRefused)
{
    EXPECT_THROW(logos::qtArgFromVariant<qlonglong>(QVariant(3.7), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, NumberForBoolIsRefused)
{
    EXPECT_THROW(logos::qtArgFromVariant<bool>(QVariant(1), "arg0"),
                 logos::CodecError);
    EXPECT_THROW(logos::qtArgFromVariant<bool>(QVariant(0), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, StringForBoolIsRefused)
{
    EXPECT_THROW(logos::qtArgFromVariant<bool>(QVariant("hello"), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, StringForIntIsRefused)
{
    // "abc" used to become 0 — the value a caller most wants to be told about.
    EXPECT_THROW(logos::qtArgFromVariant<qlonglong>(QVariant("abc"), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, NumberForStringIsRefused)
{
    EXPECT_THROW(logos::qtArgFromVariant<QString>(QVariant(5), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, OutOfRangeForNarrowIntIsRefused)
{
    // int is int32. 2^32 used to arrive as 0 — a truncation, not a clamp.
    EXPECT_THROW(logos::qtArgFromVariant<int>(QVariant(qlonglong(4294967296LL)), "arg0"),
                 logos::CodecError);
}

// -- the four typed-ARRAY cases the CLI cannot express -------------------------

TEST_F(QtArgFromVariantTest, NonStringElementInStringListIsRefused)
{
    // echoStringList(["a", 1]) — used to arrive as ["a", "1"].
    QVariantList mixed{QVariant("a"), QVariant(1)};
    EXPECT_THROW(logos::qtArgFromVariant<QStringList>(QVariant(mixed), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, StringForListIsRefused)
{
    // echoList("notalist") — used to arrive as the string split into characters.
    EXPECT_THROW(logos::qtArgFromVariant<QVariantList>(QVariant("notalist"), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, NumberForMapIsRefused)
{
    // echoMap(5) — used to arrive as {}.
    EXPECT_THROW(logos::qtArgFromVariant<QVariantMap>(QVariant(5), "arg0"),
                 logos::CodecError);
}

TEST_F(QtArgFromVariantTest, TypedElementListIsCheckedWhenTheElementTypeSurvives)
{
    // The remaining three of the nine — echoUintList([1,-1,3]),
    // echoUintList([1,2.5,3]), echoIntList([1,3.7,3]).
    //
    // A C++ signature spells all of these QVariantList, so the ELEMENT type is
    // gone by the time the generated dispatch or the QMetaObject dispatch sees
    // it: qtArgFromVariant<QVariantList> can only check array-ness, and does.
    // qtArgListOf<Elem> is where a caller that still knows the element type
    // (the LIDL-driven generator) spends it.
    QVariantList negative{QVariant(1), QVariant(-1), QVariant(3)};
    QVariantList fractional{QVariant(1), QVariant(2.5), QVariant(3)};
    QVariantList good{QVariant(1), QVariant(2), QVariant(3)};

    // What the Qt spelling can express: array-ness only, so these pass.
    EXPECT_NO_THROW(logos::qtArgFromVariant<QVariantList>(QVariant(negative), "arg0"));
    EXPECT_NO_THROW(logos::qtArgFromVariant<QVariantList>(QVariant(fractional), "arg0"));

    // What the element type buys, when it survives.
    EXPECT_THROW(logos::qtArgListOf<uint64_t>(QVariant(negative), "arg0"),
                 logos::CodecError);
    EXPECT_THROW(logos::qtArgListOf<uint64_t>(QVariant(fractional), "arg0"),
                 logos::CodecError);
    EXPECT_THROW(logos::qtArgListOf<int64_t>(
                     QVariant(QVariantList{QVariant(1), QVariant(3.7), QVariant(3)}), "arg0"),
                 logos::CodecError);
    EXPECT_NO_THROW(logos::qtArgListOf<uint64_t>(QVariant(good), "arg0"));
}

// -- acceptances: the rule that a stricter check would get wrong ---------------

TEST_F(QtArgFromVariantTest, WholeValuedFloatIsALegalInteger)
{
    // JSON does not distinguish 3 from 3.0, and logoscore's argument typing
    // produces 3.0 for "3.0". Refusing this broke four working cases once.
    EXPECT_EQ(logos::qtArgFromVariant<qlonglong>(QVariant(3.0), "arg0"), 3);
    EXPECT_EQ(logos::qtArgFromVariant<int>(QVariant(42.0), "arg0"), 42);
    EXPECT_EQ(logos::qtArgFromVariant<qulonglong>(QVariant(7.0), "arg0"), 7u);
}

TEST_F(QtArgFromVariantTest, WellFormedScalarsSurviveUnchanged)
{
    EXPECT_EQ(logos::qtArgFromVariant<qlonglong>(QVariant(42), "arg0"), 42);
    EXPECT_EQ(logos::qtArgFromVariant<qlonglong>(QVariant(-7), "arg0"), -7);
    EXPECT_EQ(logos::qtArgFromVariant<qulonglong>(
                  QVariant(qulonglong(18446744073709551615ULL)), "arg0"),
              18446744073709551615ULL);
    EXPECT_TRUE(logos::qtArgFromVariant<bool>(QVariant(true), "arg0"));
    EXPECT_DOUBLE_EQ(logos::qtArgFromVariant<double>(QVariant(2.5), "arg0"), 2.5);
    // float64 accepts an integral number — the mirror of the rule above.
    EXPECT_DOUBLE_EQ(logos::qtArgFromVariant<double>(QVariant(3), "arg0"), 3.0);
    EXPECT_EQ(logos::qtArgFromVariant<QString>(QVariant("hi"), "arg0"), QStringLiteral("hi"));
}

TEST_F(QtArgFromVariantTest, BytesKeepTheCodecsLenientForm)
{
    // NOT a refusal, deliberately: Codec<std::vector<uint8_t>> is lenient
    // because a Qt consumer passing a QString and a CLI typing its arguments
    // both produce a plain scalar for a byte parameter. Inheriting that here
    // rather than re-deciding it is the point.
    EXPECT_EQ(logos::qtArgFromVariant<QByteArray>(QVariant("hi"), "arg0"), QByteArray("hi"));
    EXPECT_EQ(logos::qtArgFromVariant<QByteArray>(QVariant(42), "arg0"), QByteArray("42"));
    EXPECT_EQ(logos::qtArgFromVariant<QByteArray>(QVariant(QByteArray("\x00\xff", 2)), "arg0"),
              QByteArray("\x00\xff", 2));
}

TEST_F(QtArgFromVariantTest, AnyIsHandedOnVerbatim)
{
    // `any` declares nothing, so there is nothing to check it against — and it
    // must not be round-tripped through JSON either: a one-key "_bytes" map is
    // indistinguishable from tagged bytes, so a round trip would silently
    // retype a user's map (registry entry M3).
    QVariantMap bytesLookalike;
    bytesLookalike.insert(QStringLiteral("_bytes"), QStringLiteral("aGk"));
    const QVariant in(bytesLookalike);

    const QVariant out = logos::qtArgFromVariant<QVariant>(in, "arg0");
    EXPECT_EQ(out.userType(), QMetaType::QVariantMap);
    EXPECT_EQ(out.toMap().value(QStringLiteral("_bytes")).toString(), QStringLiteral("aGk"));
}

TEST_F(QtArgFromVariantTest, ContainersAreHandedOnUnchangedOnceTheShapeIsChecked)
{
    // Element types are erased by the Qt spelling, so rebuilding the payload
    // from JSON would retype nested elements (int -> qlonglong) for no
    // validation gain. The caller's own value comes back.
    QVariantList list{QVariant(1), QVariant("a"), QVariant(true)};
    const QVariantList out = logos::qtArgFromVariant<QVariantList>(QVariant(list), "arg0");
    ASSERT_EQ(out.size(), 3);
    EXPECT_EQ(out.at(0).userType(), QMetaType::Int);
    EXPECT_EQ(out.at(1).toString(), QStringLiteral("a"));
    EXPECT_TRUE(out.at(2).toBool());
}

TEST_F(QtArgFromVariantTest, RejectionNamesTheSlotAndTheNestedPath)
{
    try {
        logos::qtArgFromVariant<QStringList>(
            QVariant(QVariantList{QVariant("a"), QVariant(1)}), "arg2");
        FAIL() << "expected a CodecError";
    } catch (const logos::CodecError& e) {
        // The path is what makes a failure inside a container actionable.
        EXPECT_NE(std::string(e.what()).find("arg2[1]"), std::string::npos)
            << "diagnostic was: " << e.what();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. The QMetaObject dispatch — QtProviderObject::callMethod. This is the
//    sink every legacy `type: core` plugin with no `interface` key reaches,
//    including capability_module in every runtime.
// ═══════════════════════════════════════════════════════════════════════════

class ArgValidationPlugin : public QObject, public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
public:
    QString name() const override { return "argval_mod"; }
    QString version() const override { return "1.0.0"; }

    Q_INVOKABLE int echoInt(int v) { calls++; lastInt = v; return v; }
    Q_INVOKABLE bool echoBool(bool v) { calls++; lastBool = v; return v; }
    Q_INVOKABLE int stringLength(const QString& s) { calls++; lastString = s; return s.size(); }
    Q_INVOKABLE QString joinStrings(const QStringList& parts) { calls++; lastList = parts; return parts.join(","); }
    Q_INVOKABLE int listSize(const QVariantList& v) { calls++; return int(v.size()); }
    Q_INVOKABLE int mapSize(const QVariantMap& v) { calls++; return int(v.size()); }
    Q_INVOKABLE int byteArraySize(const QByteArray& v) { calls++; return int(v.size()); }
    Q_INVOKABLE QString urlToString(const QUrl& u) { calls++; return u.toString(); }

    int calls = 0;
    int lastInt = 0;
    bool lastBool = false;
    QString lastString;
    QStringList lastList;
};

class QtProviderObjectArgTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_plugin = new ArgValidationPlugin();
        // callMethod bails early when logosAPI is null, so absent validation
        // dispatch would genuinely reach the method.
        m_api = new LogosAPI("argval_mod");
        m_plugin->logosAPI = m_api;
        m_provider = new QtProviderObject(m_plugin);
    }
    void TearDown() override
    {
        delete m_provider;
        delete m_plugin;
        delete m_api;
    }
    ArgValidationPlugin* m_plugin = nullptr;
    QtProviderObject* m_provider = nullptr;
    LogosAPI* m_api = nullptr;
};

TEST_F(QtProviderObjectArgTest, FractionalFloatForIntIsRejected)
{
    const QVariant r = m_provider->callMethod("echoInt", {QVariant(3.7)});
    EXPECT_TRUE(isDispatchFailed(r));
    EXPECT_EQ(m_plugin->calls, 0) << "the method body must not have run";
}

TEST_F(QtProviderObjectArgTest, OutOfInt32RangeIsRejected)
{
    // Used to arrive as 0 — the worst of the coercions, because the value is
    // not merely wrong but the identity for addition.
    const QVariant r = m_provider->callMethod("echoInt", {QVariant(qlonglong(4294967296LL))});
    EXPECT_TRUE(isDispatchFailed(r));
    EXPECT_EQ(m_plugin->calls, 0);
}

TEST_F(QtProviderObjectArgTest, NumberAndStringForBoolAreRejected)
{
    EXPECT_TRUE(isDispatchFailed(m_provider->callMethod("echoBool", {QVariant(1)})));
    EXPECT_TRUE(isDispatchFailed(m_provider->callMethod("echoBool", {QVariant("hello")})));
    EXPECT_EQ(m_plugin->calls, 0);
}

TEST_F(QtProviderObjectArgTest, NumberForStringIsRejected)
{
    // stringLength(42) used to answer 2 — the length of "42".
    EXPECT_TRUE(isDispatchFailed(m_provider->callMethod("stringLength", {QVariant(42)})));
    EXPECT_EQ(m_plugin->calls, 0);
}

TEST_F(QtProviderObjectArgTest, ScalarForStringListIsRejected)
{
    // joinStrings("notalist") used to arrive as a one-element list.
    EXPECT_TRUE(isDispatchFailed(m_provider->callMethod("joinStrings", {QVariant("notalist")})));
    EXPECT_EQ(m_plugin->calls, 0);
}

TEST_F(QtProviderObjectArgTest, NonStringElementInStringListIsRejected)
{
    const QVariant r = m_provider->callMethod(
        "joinStrings", {QVariant(QVariantList{QVariant("a"), QVariant(1)})});
    EXPECT_TRUE(isDispatchFailed(r));
    EXPECT_EQ(m_plugin->calls, 0);
}

TEST_F(QtProviderObjectArgTest, ScalarForListAndMapAreRejected)
{
    EXPECT_TRUE(isDispatchFailed(m_provider->callMethod("listSize", {QVariant("notalist")})));
    EXPECT_TRUE(isDispatchFailed(m_provider->callMethod("mapSize", {QVariant(5)})));
    EXPECT_EQ(m_plugin->calls, 0);
}

TEST_F(QtProviderObjectArgTest, RejectionCarriesTheProvidersName)
{
    const QVariant r = m_provider->callMethod("echoInt", {QVariant(3.7)});
    ASSERT_TRUE(isDispatchFailed(r));
    EXPECT_EQ(r.toMap().value(QStringLiteral("origin")).toString(),
              QStringLiteral("argval_mod"));
    EXPECT_FALSE(rejectionMessage(r).isEmpty());
}

TEST_F(QtProviderObjectArgTest, WellFormedCallsAreUnchanged)
{
    EXPECT_EQ(m_provider->callMethod("echoInt", {QVariant(42)}).toInt(), 42);
    EXPECT_EQ(m_provider->callMethod("echoInt", {QVariant(-7)}).toInt(), -7);
    // Whole-valued float for an int parameter: accepted, per the codec.
    EXPECT_EQ(m_provider->callMethod("echoInt", {QVariant(42.0)}).toInt(), 42);
    EXPECT_TRUE(m_provider->callMethod("echoBool", {QVariant(true)}).toBool());
    EXPECT_EQ(m_provider->callMethod("stringLength", {QVariant("hello")}).toInt(), 5);
    EXPECT_EQ(m_provider->callMethod("joinStrings",
                                     {QVariant(QStringList{"a", "b"})}).toString(),
              QStringLiteral("a,b"));
    EXPECT_EQ(m_provider->callMethod("listSize",
                                     {QVariant(QVariantList{QVariant(1), QVariant(2)})}).toInt(), 2);
    QVariantMap m;
    m.insert(QStringLiteral("k"), QVariant(1));
    EXPECT_EQ(m_provider->callMethod("mapSize", {QVariant(m)}).toInt(), 1);
    EXPECT_EQ(m_provider->callMethod("byteArraySize", {QVariant("hello")}).toInt(), 5);
}

TEST_F(QtProviderObjectArgTest, TypesWithNoLidlCounterpartKeepQtsOwnConversion)
{
    // QUrl has no LIDL type, so qtArgDecode reports Unchecked and the old
    // QVariant::convert path runs. Refusing it instead would break a call that
    // works today, which is why "check everything" is the wrong rule.
    const QVariant r = m_provider->callMethod("urlToString",
                                              {QVariant("http://example.com")});
    EXPECT_FALSE(isDispatchFailed(r));
    EXPECT_EQ(r.toString(), QStringLiteral("http://example.com"));
}

TEST_F(QtProviderObjectArgTest, IntrospectionIsNotArgumentDecoding)
{
    // getPluginMethods / getPluginEvents / getPluginInterface are answered
    // before any parameter is looked at; validation must not shadow them.
    EXPECT_FALSE(isDispatchFailed(m_provider->callMethod("getPluginMethods", {})));
    EXPECT_FALSE(isDispatchFailed(m_provider->callMethod("getPluginInterface", {})));
}

#include "test_provider_arg_validation.moc"
