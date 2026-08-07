#include "plain_module_api.h"

#include <QDebug>
#include <nlohmann/json.hpp>
#include "logos_qt_lp_bridge.h"

static nlohmann::json recToWire_Point(const PlainModule::Point& v);
static PlainModule::Point recFromWire_Point(const nlohmann::json& w);

static nlohmann::json recToWire_Point(const PlainModule::Point& v) {
    nlohmann::json __j = nlohmann::json::object();
    __j["x"] = logos::qt::toWire(QVariant::fromValue(v.x));
    __j["y"] = logos::qt::toWire(QVariant::fromValue(v.y));
    return __j;
}

static PlainModule::Point recFromWire_Point(const nlohmann::json& w) {
    PlainModule::Point __out;
    if (!w.is_object()) return __out;
    if (w.contains("x")) __out.x = logos::qt::fromWire<double>(w.at("x"));
    if (w.contains("y")) __out.y = logos::qt::fromWire<double>(w.at("y"));
    return __out;
}

PlainModule::PlainModule(LogosAPI* api)
    : m_api(api),
      m_moduleName(QStringLiteral("plain_module")),
      m_bridge(logos::qt::LpBridge::forTarget(api, QStringLiteral("plain_module"))) {}

bool PlainModule::on(const QString& eventName, RawEventCallback callback) {
    if (!callback) {
        qWarning() << "PlainModule: ignoring empty event callback for" << eventName;
        return false;
    }
    const QString _name = eventName;
    return logos::qt::subscribe(m_bridge, eventName.toStdString(),
        [callback, _name](nlohmann::json _a) {
            callback(_name, logos::nlohmannArgsToQVariantList(_a));
        });
}

bool PlainModule::on(const QString& eventName, EventCallback callback) {
    if (!callback) {
        qWarning() << "PlainModule: ignoring empty event callback for" << eventName;
        return false;
    }
    return on(eventName, [callback](const QString&, const QVariantList& data) {
        callback(data);
    });
}

bool PlainModule::onMoved(std::function<void(const Point& from, const Point& to)> callback) {
    if (!callback) {
        qWarning() << "PlainModule: ignoring empty event callback for" << QStringLiteral("moved");
        return false;
    }
    return logos::qt::subscribe(m_bridge, "moved", [callback](nlohmann::json _a) {
        if (!_a.is_array() || _a.size() < 2) return;
        callback(recFromWire_Point(_a.at(0)), recFromWire_Point(_a.at(1)));
    });
}

QString PlainModule::echo_text(const QString& s, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(s)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_text", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_text: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QString>(_r);
}

void PlainModule::echo_textAsync(const QString& s, std::function<void(QString)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(s)));
    logos::qt::invokeAsync(m_bridge, "echo_text", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<QString>(_r));
        }, timeout.ms);
}

QByteArray PlainModule::echo_bytes(QByteArray b, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(b)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_bytes", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_bytes: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QByteArray>(_r);
}

void PlainModule::echo_bytesAsync(QByteArray b, std::function<void(QByteArray)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(b)));
    logos::qt::invokeAsync(m_bridge, "echo_bytes", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<QByteArray>(_r));
        }, timeout.ms);
}

qlonglong PlainModule::echo_int(qlonglong n, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(n)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_int", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_int: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<qlonglong>(_r);
}

void PlainModule::echo_intAsync(qlonglong n, std::function<void(qlonglong)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(n)));
    logos::qt::invokeAsync(m_bridge, "echo_int", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<qlonglong>(_r));
        }, timeout.ms);
}

qulonglong PlainModule::echo_uint(qulonglong n, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(n)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_uint", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_uint: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<qulonglong>(_r);
}

void PlainModule::echo_uintAsync(qulonglong n, std::function<void(qulonglong)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(n)));
    logos::qt::invokeAsync(m_bridge, "echo_uint", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<qulonglong>(_r));
        }, timeout.ms);
}

bool PlainModule::echo_bool(bool b, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(b)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_bool", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_bool: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<bool>(_r);
}

void PlainModule::echo_boolAsync(bool b, std::function<void(bool)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(b)));
    logos::qt::invokeAsync(m_bridge, "echo_bool", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<bool>(_r));
        }, timeout.ms);
}

double PlainModule::echo_float(double f, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(f)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_float", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_float: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<double>(_r);
}

void PlainModule::echo_floatAsync(double f, std::function<void(double)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(f)));
    logos::qt::invokeAsync(m_bridge, "echo_float", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<double>(_r));
        }, timeout.ms);
}

QStringList PlainModule::echo_strings(const QStringList& v, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(v)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_strings", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_strings: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QStringList>(_r);
}

void PlainModule::echo_stringsAsync(const QStringList& v, std::function<void(QStringList)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(v)));
    logos::qt::invokeAsync(m_bridge, "echo_strings", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<QStringList>(_r));
        }, timeout.ms);
}

QVariantList PlainModule::echo_ints(const QVariantList& v, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(v)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_ints", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_ints: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QVariantList>(_r);
}

void PlainModule::echo_intsAsync(const QVariantList& v, std::function<void(QVariantList)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(v)));
    logos::qt::invokeAsync(m_bridge, "echo_ints", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<QVariantList>(_r));
        }, timeout.ms);
}

PlainModule::Point PlainModule::translate(const Point& p, double dx, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_Point(p));
    _args.push_back(logos::qt::toWire(QVariant::fromValue(dx)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "translate", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::translate: remote call failed:" << QString::fromStdString(_err.message);
    return recFromWire_Point(_r);
}

void PlainModule::translateAsync(const Point& p, double dx, std::function<void(Point)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_Point(p));
    _args.push_back(logos::qt::toWire(QVariant::fromValue(dx)));
    logos::qt::invokeAsync(m_bridge, "translate", _args,
        [callback](nlohmann::json _r) {
            callback(recFromWire_Point(_r));
        }, timeout.ms);
}

PlainModule::Point PlainModule::bounds(const QList<Point>& points, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back([&]{ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : points) __acc.push_back(recToWire_Point(__e)); return __acc; }());
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "bounds", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::bounds: remote call failed:" << QString::fromStdString(_err.message);
    return recFromWire_Point(_r);
}

void PlainModule::boundsAsync(const QList<Point>& points, std::function<void(Point)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back([&]{ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : points) __acc.push_back(recToWire_Point(__e)); return __acc; }());
    logos::qt::invokeAsync(m_bridge, "bounds", _args,
        [callback](nlohmann::json _r) {
            callback(recFromWire_Point(_r));
        }, timeout.ms);
}

QVariantMap PlainModule::attributes(const QVariantMap& tags, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(tags)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "attributes", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::attributes: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QVariantMap>(_r);
}

void PlainModule::attributesAsync(const QVariantMap& tags, std::function<void(QVariantMap)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(tags)));
    logos::qt::invokeAsync(m_bridge, "attributes", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<QVariantMap>(_r));
        }, timeout.ms);
}

QVariant PlainModule::describe(const Point& p, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_Point(p));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "describe", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::describe: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QVariant>(_r);
}

void PlainModule::describeAsync(const Point& p, std::function<void(QVariant)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_Point(p));
    logos::qt::invokeAsync(m_bridge, "describe", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<QVariant>(_r));
        }, timeout.ms);
}

LogosResult PlainModule::fetch(const QString& id, logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(id)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "fetch", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::fetch: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<LogosResult>(_r);
}

void PlainModule::fetchAsync(const QString& id, std::function<void(LogosResult)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(id)));
    logos::qt::invokeAsync(m_bridge, "fetch", _args,
        [callback](nlohmann::json _r) {
            callback(logos::qt::fromWire<LogosResult>(_r));
        }, timeout.ms);
}

void PlainModule::reset(logos::CallError* err) {
    nlohmann::json _args = nlohmann::json::array();
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "reset", _args, &_err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::reset: remote call failed:" << QString::fromStdString(_err.message);
    (void)_r;
}

void PlainModule::resetAsync(std::function<void()> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    logos::qt::invokeAsync(m_bridge, "reset", _args,
        [callback](nlohmann::json _r) {
            (void)_r; callback();
        }, timeout.ms);
}

