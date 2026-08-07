#pragma once
#include <QString>
#include <QVariant>
#include <QStringList>
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <utility>
#include "logos_types.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_call_error.h"
#include "logos_object.h"

namespace logos { namespace qt { class LpBridge; } }

class PlainModule {
public:
    // Record types declared by the contract.
    struct Point {
        double x{};
        double y{};
    };

    explicit PlainModule(LogosAPI* api);

    using RawEventCallback = std::function<void(const QString&, const QVariantList&)>;
    using EventCallback = std::function<void(const QVariantList&)>;

    bool on(const QString& eventName, RawEventCallback callback);
    bool on(const QString& eventName, EventCallback callback);
    bool onMoved(std::function<void(const Point& from, const Point& to)> callback);

    QString echo_text(const QString& s, logos::CallError* err = nullptr);
    void echo_textAsync(const QString& s, std::function<void(QString)> callback, Timeout timeout = Timeout());
    QByteArray echo_bytes(QByteArray b, logos::CallError* err = nullptr);
    void echo_bytesAsync(QByteArray b, std::function<void(QByteArray)> callback, Timeout timeout = Timeout());
    qlonglong echo_int(qlonglong n, logos::CallError* err = nullptr);
    void echo_intAsync(qlonglong n, std::function<void(qlonglong)> callback, Timeout timeout = Timeout());
    qulonglong echo_uint(qulonglong n, logos::CallError* err = nullptr);
    void echo_uintAsync(qulonglong n, std::function<void(qulonglong)> callback, Timeout timeout = Timeout());
    bool echo_bool(bool b, logos::CallError* err = nullptr);
    void echo_boolAsync(bool b, std::function<void(bool)> callback, Timeout timeout = Timeout());
    double echo_float(double f, logos::CallError* err = nullptr);
    void echo_floatAsync(double f, std::function<void(double)> callback, Timeout timeout = Timeout());
    QStringList echo_strings(const QStringList& v, logos::CallError* err = nullptr);
    void echo_stringsAsync(const QStringList& v, std::function<void(QStringList)> callback, Timeout timeout = Timeout());
    QVariantList echo_ints(const QVariantList& v, logos::CallError* err = nullptr);
    void echo_intsAsync(const QVariantList& v, std::function<void(QVariantList)> callback, Timeout timeout = Timeout());
    Point translate(const Point& p, double dx, logos::CallError* err = nullptr);
    void translateAsync(const Point& p, double dx, std::function<void(Point)> callback, Timeout timeout = Timeout());
    Point bounds(const QList<Point>& points, logos::CallError* err = nullptr);
    void boundsAsync(const QList<Point>& points, std::function<void(Point)> callback, Timeout timeout = Timeout());
    QVariantMap attributes(const QVariantMap& tags, logos::CallError* err = nullptr);
    void attributesAsync(const QVariantMap& tags, std::function<void(QVariantMap)> callback, Timeout timeout = Timeout());
    QVariant describe(const Point& p, logos::CallError* err = nullptr);
    void describeAsync(const Point& p, std::function<void(QVariant)> callback, Timeout timeout = Timeout());
    LogosResult fetch(const QString& id, logos::CallError* err = nullptr);
    void fetchAsync(const QString& id, std::function<void(LogosResult)> callback, Timeout timeout = Timeout());
    void reset(logos::CallError* err = nullptr);
    void resetAsync(std::function<void()> callback, Timeout timeout = Timeout());

private:
    LogosAPI* m_api;
    QString m_moduleName;
    logos::qt::LpBridge* m_bridge;
};
