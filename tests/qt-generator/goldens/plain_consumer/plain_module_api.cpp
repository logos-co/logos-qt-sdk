#include "plain_module_api.h"

#include <QDebug>
#include <nlohmann/json.hpp>
#include "logos_qt_lp_bridge.h"

#ifndef LOGOS_GENERATED_DISPATCH_REJECTION_JSON
#define LOGOS_GENERATED_DISPATCH_REJECTION_JSON

namespace {

bool logosDispatchRejectionJson(const nlohmann::json& v, logos::CallError& out)
{
    if (!v.is_object() || v.size() != 3) return false;
    auto code = v.find("code"), message = v.find("message"), origin = v.find("origin");
    if (code == v.end() || message == v.end() || origin == v.end()) return false;
    if (!code->is_string() || !message->is_string() || !origin->is_string()) return false;
    const std::string _code = code->get<std::string>();
    if (_code != "dispatch_failed"
        && _code != "invalid_args"
        && _code != "unknown_method") return false;
    out.code = _code;
    out.message = message->get<std::string>();
    out.origin = origin->get<std::string>();
    return true;
}

} // namespace

#endif  // LOGOS_GENERATED_DISPATCH_REJECTION_JSON

#ifndef LOGOS_GENERATED_DECODE_FAILURE_JSON
#define LOGOS_GENERATED_DECODE_FAILURE_JSON

namespace {

void logosNoteDecodeFailure(const std::string& why, const std::string& origin,
                            logos::CallError& out)
{
    if (why.empty() || !out.ok()) return;
    out.code = "decode_failed";
    out.message = why;
    out.origin = origin;
}

} // namespace

#endif  // LOGOS_GENERATED_DECODE_FAILURE_JSON

static nlohmann::json recToWire_PlainModule_Point(const PlainModule::Point& v);
static PlainModule::Point recFromWire_PlainModule_Point(const nlohmann::json& w, std::string* __derr = nullptr);

static nlohmann::json recToWire_PlainModule_Point(const PlainModule::Point& v) {
    nlohmann::json __j = nlohmann::json::object();
    __j["x"] = logos::qt::toWire(QVariant::fromValue(v.x));
    __j["y"] = logos::qt::toWire(QVariant::fromValue(v.y));
    return __j;
}

static PlainModule::Point recFromWire_PlainModule_Point(const nlohmann::json& w, std::string* __derr) {
    PlainModule::Point __out;
    std::string __why;
    if (!logos::qt::tryRequireObject(w, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `Point` value: " + __why); qWarning() << "PlainModule: rejected a `Point` value:" << QString::fromStdString(__why); return __out; }
    if (w.contains("x")) { double __v{}; if (logos::qt::tryFromWire(w.at("x"), __v, &__why)) __out.x = __v; else { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `float64` field `x`: " + __why); qWarning() << "PlainModule: rejected a `float64` field `x`:" << QString::fromStdString(__why); } }
    if (w.contains("y")) { double __v{}; if (logos::qt::tryFromWire(w.at("y"), __v, &__why)) __out.y = __v; else { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `float64` field `y`: " + __why); qWarning() << "PlainModule: rejected a `float64` field `y`:" << QString::fromStdString(__why); } }
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
        std::string* __derr = nullptr;
        callback(recFromWire_PlainModule_Point(_a.at(0), __derr), recFromWire_PlainModule_Point(_a.at(1), __derr));
    });
}

QString PlainModule::echo_text(const QString& s, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(s)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_text", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
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
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::echo_textAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<QString>(_r));
        }, timeout.ms);
}

void PlainModule::echo_textAsyncResult(const QString& s, std::function<void(logos::AsyncResult<QString>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(s)));
    logos::qt::invokeAsyncResult(m_bridge, "echo_text", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<QString> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<QString>(_r);
            callback(_res);
        }, timeout.ms);
}

QByteArray PlainModule::echo_bytes(QByteArray b, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(b)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_bytes", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
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
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::echo_bytesAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<QByteArray>(_r));
        }, timeout.ms);
}

void PlainModule::echo_bytesAsyncResult(QByteArray b, std::function<void(logos::AsyncResult<QByteArray>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(b)));
    logos::qt::invokeAsyncResult(m_bridge, "echo_bytes", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<QByteArray> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<QByteArray>(_r);
            callback(_res);
        }, timeout.ms);
}

qlonglong PlainModule::echo_int(qlonglong n, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(n)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_int", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
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
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::echo_intAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<qlonglong>(_r));
        }, timeout.ms);
}

void PlainModule::echo_intAsyncResult(qlonglong n, std::function<void(logos::AsyncResult<qlonglong>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(n)));
    logos::qt::invokeAsyncResult(m_bridge, "echo_int", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<qlonglong> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<qlonglong>(_r);
            callback(_res);
        }, timeout.ms);
}

qulonglong PlainModule::echo_uint(qulonglong n, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(n)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_uint", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
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
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::echo_uintAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<qulonglong>(_r));
        }, timeout.ms);
}

void PlainModule::echo_uintAsyncResult(qulonglong n, std::function<void(logos::AsyncResult<qulonglong>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(n)));
    logos::qt::invokeAsyncResult(m_bridge, "echo_uint", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<qulonglong> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<qulonglong>(_r);
            callback(_res);
        }, timeout.ms);
}

bool PlainModule::echo_bool(bool b, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(b)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_bool", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
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
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::echo_boolAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<bool>(_r));
        }, timeout.ms);
}

void PlainModule::echo_boolAsyncResult(bool b, std::function<void(logos::AsyncResult<bool>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(b)));
    logos::qt::invokeAsyncResult(m_bridge, "echo_bool", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<bool> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<bool>(_r);
            callback(_res);
        }, timeout.ms);
}

double PlainModule::echo_float(double f, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(f)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_float", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
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
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::echo_floatAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<double>(_r));
        }, timeout.ms);
}

void PlainModule::echo_floatAsyncResult(double f, std::function<void(logos::AsyncResult<double>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(f)));
    logos::qt::invokeAsyncResult(m_bridge, "echo_float", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<double> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<double>(_r);
            callback(_res);
        }, timeout.ms);
}

QStringList PlainModule::echo_strings(const QStringList& v, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(v)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_strings", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_strings: remote call failed:" << QString::fromStdString(_err.message);
    std::string _derr;
    std::string* __derr = &_derr;
    QStringList _out = [&](const nlohmann::json& __s){ QStringList __acc; std::string __why; if (!logos::qt::tryFromWire(__s, __acc, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `[tstr]` value: " + __why); qWarning() << "PlainModule: rejected a `[tstr]` value:" << QString::fromStdString(__why); return QStringList(); } return __acc; }(_r);
    if (err) logosNoteDecodeFailure(_derr, m_moduleName.toStdString(), *err);
    return _out;
}

void PlainModule::echo_stringsAsync(const QStringList& v, std::function<void(QStringList)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(v)));
    logos::qt::invokeAsync(m_bridge, "echo_strings", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::echo_stringsAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            std::string* __derr = nullptr;
            callback([&](const nlohmann::json& __s){ QStringList __acc; std::string __why; if (!logos::qt::tryFromWire(__s, __acc, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `[tstr]` value: " + __why); qWarning() << "PlainModule: rejected a `[tstr]` value:" << QString::fromStdString(__why); return QStringList(); } return __acc; }(_r));
        }, timeout.ms);
}

void PlainModule::echo_stringsAsyncResult(const QStringList& v, std::function<void(logos::AsyncResult<QStringList>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(v)));
    logos::qt::invokeAsyncResult(m_bridge, "echo_strings", _args,
        [callback, _target = m_moduleName.toStdString()](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<QStringList> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            std::string _derr;
            std::string* __derr = &_derr;
            _res.value = [&](const nlohmann::json& __s){ QStringList __acc; std::string __why; if (!logos::qt::tryFromWire(__s, __acc, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `[tstr]` value: " + __why); qWarning() << "PlainModule: rejected a `[tstr]` value:" << QString::fromStdString(__why); return QStringList(); } return __acc; }(_r);
            logosNoteDecodeFailure(_derr, _target, _res.error);
            callback(_res);
        }, timeout.ms);
}

QList<qlonglong> PlainModule::echo_ints(const QList<qlonglong>& v, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back([&](const auto& __c){ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : __c) __acc.push_back(logos::qt::toWire(QVariant::fromValue(__e))); return __acc; }(v));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "echo_ints", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::echo_ints: remote call failed:" << QString::fromStdString(_err.message);
    std::string _derr;
    std::string* __derr = &_derr;
    QList<qlonglong> _out = [&](const nlohmann::json& __s){ QList<qlonglong> __acc; std::string __why; if (!logos::qt::tryRequireArray(__s, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `[int]` value: " + __why); qWarning() << "PlainModule: rejected a `[int]` value:" << QString::fromStdString(__why); return __acc; } for (const auto& __e : __s) { qlonglong __v{}; if (!logos::qt::tryFromWire(__e, __v, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `int` element: " + __why); qWarning() << "PlainModule: rejected a `int` element:" << QString::fromStdString(__why); return QList<qlonglong>(); } __acc.push_back(__v); } return __acc; }(_r);
    if (err) logosNoteDecodeFailure(_derr, m_moduleName.toStdString(), *err);
    return _out;
}

void PlainModule::echo_intsAsync(const QList<qlonglong>& v, std::function<void(QList<qlonglong>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back([&](const auto& __c){ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : __c) __acc.push_back(logos::qt::toWire(QVariant::fromValue(__e))); return __acc; }(v));
    logos::qt::invokeAsync(m_bridge, "echo_ints", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::echo_intsAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            std::string* __derr = nullptr;
            callback([&](const nlohmann::json& __s){ QList<qlonglong> __acc; std::string __why; if (!logos::qt::tryRequireArray(__s, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `[int]` value: " + __why); qWarning() << "PlainModule: rejected a `[int]` value:" << QString::fromStdString(__why); return __acc; } for (const auto& __e : __s) { qlonglong __v{}; if (!logos::qt::tryFromWire(__e, __v, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `int` element: " + __why); qWarning() << "PlainModule: rejected a `int` element:" << QString::fromStdString(__why); return QList<qlonglong>(); } __acc.push_back(__v); } return __acc; }(_r));
        }, timeout.ms);
}

void PlainModule::echo_intsAsyncResult(const QList<qlonglong>& v, std::function<void(logos::AsyncResult<QList<qlonglong>>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back([&](const auto& __c){ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : __c) __acc.push_back(logos::qt::toWire(QVariant::fromValue(__e))); return __acc; }(v));
    logos::qt::invokeAsyncResult(m_bridge, "echo_ints", _args,
        [callback, _target = m_moduleName.toStdString()](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<QList<qlonglong>> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            std::string _derr;
            std::string* __derr = &_derr;
            _res.value = [&](const nlohmann::json& __s){ QList<qlonglong> __acc; std::string __why; if (!logos::qt::tryRequireArray(__s, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `[int]` value: " + __why); qWarning() << "PlainModule: rejected a `[int]` value:" << QString::fromStdString(__why); return __acc; } for (const auto& __e : __s) { qlonglong __v{}; if (!logos::qt::tryFromWire(__e, __v, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `int` element: " + __why); qWarning() << "PlainModule: rejected a `int` element:" << QString::fromStdString(__why); return QList<qlonglong>(); } __acc.push_back(__v); } return __acc; }(_r);
            logosNoteDecodeFailure(_derr, _target, _res.error);
            callback(_res);
        }, timeout.ms);
}

PlainModule::Point PlainModule::translate(const Point& p, double dx, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_PlainModule_Point(p));
    _args.push_back(logos::qt::toWire(QVariant::fromValue(dx)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "translate", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::translate: remote call failed:" << QString::fromStdString(_err.message);
    std::string _derr;
    std::string* __derr = &_derr;
    PlainModule::Point _out = recFromWire_PlainModule_Point(_r, __derr);
    if (err) logosNoteDecodeFailure(_derr, m_moduleName.toStdString(), *err);
    return _out;
}

void PlainModule::translateAsync(const Point& p, double dx, std::function<void(Point)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_PlainModule_Point(p));
    _args.push_back(logos::qt::toWire(QVariant::fromValue(dx)));
    logos::qt::invokeAsync(m_bridge, "translate", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::translateAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            std::string* __derr = nullptr;
            callback(recFromWire_PlainModule_Point(_r, __derr));
        }, timeout.ms);
}

void PlainModule::translateAsyncResult(const Point& p, double dx, std::function<void(logos::AsyncResult<Point>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_PlainModule_Point(p));
    _args.push_back(logos::qt::toWire(QVariant::fromValue(dx)));
    logos::qt::invokeAsyncResult(m_bridge, "translate", _args,
        [callback, _target = m_moduleName.toStdString()](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<Point> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            std::string _derr;
            std::string* __derr = &_derr;
            _res.value = recFromWire_PlainModule_Point(_r, __derr);
            logosNoteDecodeFailure(_derr, _target, _res.error);
            callback(_res);
        }, timeout.ms);
}

PlainModule::Point PlainModule::bounds(const QList<Point>& points, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back([&](const auto& __c){ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : __c) __acc.push_back(recToWire_PlainModule_Point(__e)); return __acc; }(points));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "bounds", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::bounds: remote call failed:" << QString::fromStdString(_err.message);
    std::string _derr;
    std::string* __derr = &_derr;
    PlainModule::Point _out = recFromWire_PlainModule_Point(_r, __derr);
    if (err) logosNoteDecodeFailure(_derr, m_moduleName.toStdString(), *err);
    return _out;
}

void PlainModule::boundsAsync(const QList<Point>& points, std::function<void(Point)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back([&](const auto& __c){ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : __c) __acc.push_back(recToWire_PlainModule_Point(__e)); return __acc; }(points));
    logos::qt::invokeAsync(m_bridge, "bounds", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::boundsAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            std::string* __derr = nullptr;
            callback(recFromWire_PlainModule_Point(_r, __derr));
        }, timeout.ms);
}

void PlainModule::boundsAsyncResult(const QList<Point>& points, std::function<void(logos::AsyncResult<Point>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back([&](const auto& __c){ nlohmann::json __acc = nlohmann::json::array(); for (const auto& __e : __c) __acc.push_back(recToWire_PlainModule_Point(__e)); return __acc; }(points));
    logos::qt::invokeAsyncResult(m_bridge, "bounds", _args,
        [callback, _target = m_moduleName.toStdString()](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<Point> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            std::string _derr;
            std::string* __derr = &_derr;
            _res.value = recFromWire_PlainModule_Point(_r, __derr);
            logosNoteDecodeFailure(_derr, _target, _res.error);
            callback(_res);
        }, timeout.ms);
}

QVariantMap PlainModule::attributes(const QVariantMap& tags, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(tags)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "attributes", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::attributes: remote call failed:" << QString::fromStdString(_err.message);
    std::string _derr;
    std::string* __derr = &_derr;
    QVariantMap _out = [&](const nlohmann::json& __s){ QVariantMap __acc; std::string __why; if (!logos::qt::tryFromWire(__s, __acc, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `{tstr: any}` value: " + __why); qWarning() << "PlainModule: rejected a `{tstr: any}` value:" << QString::fromStdString(__why); return QVariantMap(); } return __acc; }(_r);
    if (err) logosNoteDecodeFailure(_derr, m_moduleName.toStdString(), *err);
    return _out;
}

void PlainModule::attributesAsync(const QVariantMap& tags, std::function<void(QVariantMap)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(tags)));
    logos::qt::invokeAsync(m_bridge, "attributes", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::attributesAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            std::string* __derr = nullptr;
            callback([&](const nlohmann::json& __s){ QVariantMap __acc; std::string __why; if (!logos::qt::tryFromWire(__s, __acc, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `{tstr: any}` value: " + __why); qWarning() << "PlainModule: rejected a `{tstr: any}` value:" << QString::fromStdString(__why); return QVariantMap(); } return __acc; }(_r));
        }, timeout.ms);
}

void PlainModule::attributesAsyncResult(const QVariantMap& tags, std::function<void(logos::AsyncResult<QVariantMap>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(tags)));
    logos::qt::invokeAsyncResult(m_bridge, "attributes", _args,
        [callback, _target = m_moduleName.toStdString()](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<QVariantMap> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            std::string _derr;
            std::string* __derr = &_derr;
            _res.value = [&](const nlohmann::json& __s){ QVariantMap __acc; std::string __why; if (!logos::qt::tryFromWire(__s, __acc, &__why)) { logos::qt::noteDecodeError(__derr, "PlainModule: rejected a `{tstr: any}` value: " + __why); qWarning() << "PlainModule: rejected a `{tstr: any}` value:" << QString::fromStdString(__why); return QVariantMap(); } return __acc; }(_r);
            logosNoteDecodeFailure(_derr, _target, _res.error);
            callback(_res);
        }, timeout.ms);
}

QVariant PlainModule::describe(const Point& p, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_PlainModule_Point(p));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "describe", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::describe: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QVariant>(_r);
}

void PlainModule::describeAsync(const Point& p, std::function<void(QVariant)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_PlainModule_Point(p));
    logos::qt::invokeAsync(m_bridge, "describe", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::describeAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<QVariant>(_r));
        }, timeout.ms);
}

void PlainModule::describeAsyncResult(const Point& p, std::function<void(logos::AsyncResult<QVariant>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(recToWire_PlainModule_Point(p));
    logos::qt::invokeAsyncResult(m_bridge, "describe", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<QVariant> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<QVariant>(_r);
            callback(_res);
        }, timeout.ms);
}

LogosResult PlainModule::fetch(const QString& id, logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(id)));
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "fetch", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
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
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::fetchAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<LogosResult>(_r));
        }, timeout.ms);
}

void PlainModule::fetchAsyncResult(const QString& id, std::function<void(logos::AsyncResult<LogosResult>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    _args.push_back(logos::qt::toWire(QVariant::fromValue(id)));
    logos::qt::invokeAsyncResult(m_bridge, "fetch", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<LogosResult> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<LogosResult>(_r);
            callback(_res);
        }, timeout.ms);
}

void PlainModule::reset(logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "reset", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::reset: remote call failed:" << QString::fromStdString(_err.message);
    (void)_r;
}

void PlainModule::resetAsync(std::function<void()> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    logos::qt::invokeAsync(m_bridge, "reset", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::resetAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            (void)_r; callback();
        }, timeout.ms);
}

void PlainModule::resetAsyncResult(std::function<void(logos::AsyncResult<void>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    logos::qt::invokeAsyncResult(m_bridge, "reset", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<void> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            (void)_r;
            callback(_res);
        }, timeout.ms);
}

QString PlainModule::name(logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "name", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::name: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QString>(_r);
}

void PlainModule::nameAsync(std::function<void(QString)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    logos::qt::invokeAsync(m_bridge, "name", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::nameAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<QString>(_r));
        }, timeout.ms);
}

void PlainModule::nameAsyncResult(std::function<void(logos::AsyncResult<QString>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    logos::qt::invokeAsyncResult(m_bridge, "name", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<QString> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<QString>(_r);
            callback(_res);
        }, timeout.ms);
}

QString PlainModule::version(logos::CallError* err, Timeout timeout) {
    nlohmann::json _args = nlohmann::json::array();
    logos::CallError _err;
    nlohmann::json _r = logos::qt::invoke(m_bridge, "version", _args, &_err, timeout.ms);
    if (_err.ok()) logosDispatchRejectionJson(_r, _err);
    if (err) *err = _err;
    else if (!_err.ok()) qWarning() << "PlainModule::version: remote call failed:" << QString::fromStdString(_err.message);
    return logos::qt::fromWire<QString>(_r);
}

void PlainModule::versionAsync(std::function<void(QString)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    logos::qt::invokeAsync(m_bridge, "version", _args,
        [callback](nlohmann::json _r) {
            { logos::CallError _rej; if (logosDispatchRejectionJson(_r, _rej))
                  qWarning() << "PlainModule::versionAsync: remote call failed:" << QString::fromStdString(_rej.message); }
            callback(logos::qt::fromWire<QString>(_r));
        }, timeout.ms);
}

void PlainModule::versionAsyncResult(std::function<void(logos::AsyncResult<QString>)> callback, Timeout timeout) {
    if (!callback) return;
    nlohmann::json _args = nlohmann::json::array();
    logos::qt::invokeAsyncResult(m_bridge, "version", _args,
        [callback](nlohmann::json _r, const logos::CallError& _err) {
            logos::AsyncResult<QString> _res;
            _res.error = _err;
            if (_res.error.ok()) logosDispatchRejectionJson(_r, _res.error);
            _res.value = logos::qt::fromWire<QString>(_r);
            callback(_res);
        }, timeout.ms);
}

