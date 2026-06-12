#include <gtest/gtest.h>
#include <QIODevice>
#include "logos_types.h"

TEST(LogosResultTest, GetValueSuccess)
{
    LogosResult r;
    r.success = true;
    r.value = QVariant(42);

    EXPECT_EQ(r.getValue<int>(), 42);
}

TEST(LogosResultTest, GetValueThrowsOnFailure)
{
    LogosResult r;
    r.success = false;
    r.error = QVariant("something went wrong");

    EXPECT_THROW(r.getValue<int>(), LogosResultException);
}

TEST(LogosResultTest, GetErrorOnFailure)
{
    LogosResult r;
    r.success = false;
    r.error = QVariant("bad request");

    EXPECT_EQ(r.getError<QString>(), "bad request");
}

TEST(LogosResultTest, GetErrorThrowsOnSuccess)
{
    LogosResult r;
    r.success = true;
    r.value = QVariant(1);

    EXPECT_THROW(r.getError<QString>(), LogosResultException);
}

TEST(LogosResultTest, GetString)
{
    LogosResult r;
    r.success = true;
    r.value = QVariant("hello");

    EXPECT_EQ(r.getString(), "hello");
}

TEST(LogosResultTest, GetBool)
{
    LogosResult r;
    r.success = true;
    r.value = QVariant(true);

    EXPECT_TRUE(r.getBool());
}

TEST(LogosResultTest, GetInt)
{
    LogosResult r;
    r.success = true;
    r.value = QVariant(99);

    EXPECT_EQ(r.getInt(), 99);
}

TEST(LogosResultTest, GetValueFromMapWithKey)
{
    QVariantMap map;
    map["name"] = "Alice";
    map["age"] = 30;

    LogosResult r;
    r.success = true;
    r.value = QVariant(map);

    EXPECT_EQ(r.getString("name"), "Alice");
    EXPECT_EQ(r.getInt("age"), 30);
}

TEST(LogosResultTest, GetValueFromMapWithDefault)
{
    QVariantMap map;
    map["name"] = "Bob";

    LogosResult r;
    r.success = true;
    r.value = QVariant(map);

    EXPECT_EQ(r.getString("missing", "default"), "default");
    EXPECT_EQ(r.getInt("missing", -1), -1);
}

TEST(LogosResultTest, GetValueFromListByIndex)
{
    QVariantMap item0;
    item0["id"] = 1;
    item0["label"] = "first";

    QVariantMap item1;
    item1["id"] = 2;
    item1["label"] = "second";

    QVariantList list;
    list << QVariant(item0) << QVariant(item1);

    LogosResult r;
    r.success = true;
    r.value = QVariant(list);

    EXPECT_EQ(r.getString(0, "label"), "first");
    EXPECT_EQ(r.getInt(1, "id"), 2);
}

TEST(LogosResultTest, GetValueFromListOutOfBoundsReturnsDefault)
{
    QVariantList list;
    list << QVariant(QVariantMap{{"k", "v"}});

    LogosResult r;
    r.success = true;
    r.value = QVariant(list);

    EXPECT_EQ(r.getString(99, "k", "fallback"), "fallback");
    EXPECT_EQ(r.getInt(-1, "k", -1), -1);
}

TEST(LogosResultTest, GetList)
{
    QVariantList list;
    list << 1 << 2 << 3;

    LogosResult r;
    r.success = true;
    r.value = QVariant(list);

    EXPECT_EQ(r.getList().size(), 3);
}

TEST(LogosResultTest, GetMap)
{
    QVariantMap map;
    map["a"] = 1;

    LogosResult r;
    r.success = true;
    r.value = QVariant(map);

    EXPECT_TRUE(r.getMap().contains("a"));
}

TEST(LogosResultTest, Serialization)
{
    LogosResult original;
    original.success = true;
    original.value = QVariant("test_value");
    original.error = QVariant();

    QByteArray buffer;
    {
        QDataStream out(&buffer, QIODevice::WriteOnly);
        out << original;
    }

    LogosResult restored;
    {
        QDataStream in(&buffer, QIODevice::ReadOnly);
        in >> restored;
    }

    EXPECT_EQ(restored.success, original.success);
    EXPECT_EQ(restored.value.toString(), "test_value");
}
