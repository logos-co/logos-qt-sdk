#include <gtest/gtest.h>

#include "logos_qt_wire.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

// The Qt <-> canonical-JSON edge a generated Qt-typed consumer wrapper is
// built from. These tests are the type table the generator DOESN'T have: they
// pin, once, that `fromWire<T>(toWire(QVariant::fromValue(x)))` returns `x` for
// every type the Qt surface exposes — so the generator can emit that one pair
// of calls for every method, every parameter and both the sync and async return
// paths, and there is nothing left for those paths to disagree about.
//
// The inputs are chosen to break a lazy implementation:
//   * uint64 max has no signed representation and no exact double,
//   * int64 min likewise,
//   * the bytes carry 0x00 / 0x80 / 0xFF (NUL truncation, non-UTF-8),
//   * `result` is checked for an ABSENT error, which is the state a
//     std::string-typed intermediate cannot carry.

using logos::qt::toWire;
using logos::qt::fromWire;

namespace {
template <class T>
T roundTrip(const T& v)
{
    return fromWire<T>(toWire(QVariant::fromValue(v)));
}
}  // namespace

TEST(QtWire, Scalars)
{
    EXPECT_EQ(roundTrip<QString>(QStringLiteral("héllo•")), QStringLiteral("héllo•"));
    EXPECT_EQ(roundTrip<bool>(true), true);
    EXPECT_EQ(roundTrip<bool>(false), false);
    EXPECT_DOUBLE_EQ(roundTrip<double>(2.5), 2.5);
}

TEST(QtWire, SixtyFourBitIntegersKeepEveryBit)
{
    // The values that discriminate a 64-bit path from a 32-bit or double one.
    EXPECT_EQ(roundTrip<qulonglong>(18446744073709551615ULL), 18446744073709551615ULL);
    EXPECT_EQ(roundTrip<qlonglong>(Q_INT64_C(-9223372036854775807) - 1),
              Q_INT64_C(-9223372036854775807) - 1);
    // Outside double's exactly-representable range: a JSON round trip through a
    // double would come back off by one.
    EXPECT_EQ(roundTrip<qlonglong>(Q_INT64_C(-9007199254740993)),
              Q_INT64_C(-9007199254740993));
}

TEST(QtWire, BytesSurviveNulAndHighBits)
{
    const QByteArray b("\x00\x80\xff", 3);
    const QByteArray back = roundTrip<QByteArray>(b);
    ASSERT_EQ(back.size(), 3);
    EXPECT_EQ(back, b);
}

TEST(QtWire, Containers)
{
    EXPECT_EQ(roundTrip<QStringList>(QStringList{"a", "b"}), (QStringList{"a", "b"}));

    QVariantList l{QVariant::fromValue(qlonglong(1)), QVariant(QStringLiteral("a")),
                   QVariant(true)};
    const QVariantList lb = roundTrip<QVariantList>(l);
    ASSERT_EQ(lb.size(), 3);
    EXPECT_EQ(lb.at(0).toLongLong(), 1);
    EXPECT_EQ(lb.at(1).toString(), QStringLiteral("a"));
    EXPECT_EQ(lb.at(2).toBool(), true);

    QVariantMap m;
    m.insert("k", QStringLiteral("v"));
    m.insert("n", QVariant::fromValue(qulonglong(18446744073709551615ULL)));
    const QVariantMap mb = roundTrip<QVariantMap>(m);
    EXPECT_EQ(mb.value("k").toString(), QStringLiteral("v"));
    EXPECT_EQ(mb.value("n").toULongLong(), 18446744073709551615ULL);
}

TEST(QtWire, AnyKeepsItsShape)
{
    // `any` is the one type where narrowing would be wrong: it may be a string,
    // a number, an object or an array, and forcing it to a map collapsed every
    // non-object value.
    EXPECT_EQ(fromWire<QVariant>(toWire(QVariant(QStringLiteral("x")))).toString(),
              QStringLiteral("x"));
    EXPECT_EQ(fromWire<QVariant>(toWire(QVariant::fromValue(qlonglong(7)))).toLongLong(), 7);
    QVariantMap obj; obj.insert("k", 1);
    EXPECT_EQ(fromWire<QVariant>(toWire(QVariant(obj))).toMap().value("k").toInt(), 1);
}

TEST(QtWire, ResultKeepsAnAbsentError)
{
    qRegisterMetaType<LogosResult>("LogosResult");

    QVariantMap payload;
    payload.insert("blob", QVariant(QByteArray("\x00\x80\xff", 3)));

    LogosResult r;
    r.success = true;
    r.value = QVariant(payload);
    // error deliberately left invalid

    const LogosResult back = roundTrip<LogosResult>(r);
    EXPECT_TRUE(back.success);
    // The state a std::string-typed hop destroys: absent, not empty.
    EXPECT_FALSE(back.error.isValid());
    ASSERT_EQ(back.value.toMap().value("blob").userType(), int(QMetaType::QByteArray));
    EXPECT_EQ(back.value.toMap().value("blob").toByteArray().size(), 3);
}

TEST(QtWire, ResultKeepsAnError)
{
    qRegisterMetaType<LogosResult>("LogosResult");

    LogosResult r;
    r.success = false;
    r.error = QVariant(QStringLiteral("boom"));

    const LogosResult back = roundTrip<LogosResult>(r);
    EXPECT_FALSE(back.success);
    EXPECT_EQ(back.error.toString(), QStringLiteral("boom"));
}

TEST(QtWire, NullDecodesToTheDefault)
{
    // Every failure path (no transport, a dropped call) yields a JSON null, and
    // the generated wrapper decodes it with the SAME expression it uses on
    // success — so this is what "the default" means, with no per-type table.
    EXPECT_EQ(fromWire<QString>(nlohmann::json()), QString());
    EXPECT_EQ(fromWire<qlonglong>(nlohmann::json()), 0);
    EXPECT_EQ(fromWire<qulonglong>(nlohmann::json()), 0u);
    EXPECT_EQ(fromWire<bool>(nlohmann::json()), false);
    EXPECT_TRUE(fromWire<QVariantList>(nlohmann::json()).isEmpty());
    EXPECT_TRUE(fromWire<QVariantMap>(nlohmann::json()).isEmpty());
    EXPECT_TRUE(fromWire<QByteArray>(nlohmann::json()).isEmpty());
    EXPECT_FALSE(fromWire<QVariant>(nlohmann::json()).isValid());
    EXPECT_FALSE(fromWire<LogosResult>(nlohmann::json()).success);
}
