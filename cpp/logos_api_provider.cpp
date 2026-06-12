#include "logos_api_provider.h"
#include "logos_object.h"
#include "logos_provider_object.h"
#include "qt_provider_object.h"
#include "module_proxy.h"
#include "logos_api.h"
#include "logos_instance.h"
#include "logos_transport.h"
#include "logos_transport_factory.h"
#include <QDebug>
#include <string>

LogosAPIProvider::LogosAPIProvider(const QString& module_name,
                                   LogosTransportSet transports,
                                   QObject *parent)
    : QObject(parent)
    , m_registryUrl(LogosInstance::id(module_name))
    , m_moduleProxy(nullptr)
    , m_qtProviderObject(nullptr)
{
    // Helper: defer-construct one host and only retain it if the
    // factory actually returned something. createHost() can return
    // nullptr — e.g. PlainTransportHost::start() failure (TCP bind,
    // SSL cert load) — and we don't want to leave a null entry that
    // would crash the publish/unpublish paths later.
    auto pushHost = [&](auto&& host, const char* label) {
        if (host) {
            m_transports.push_back(std::forward<decltype(host)>(host));
        } else {
            qWarning() << "LogosAPIProvider: createHost returned null"
                       << "for" << module_name << label
                       << "— transport disabled";
        }
    };

    if (transports.empty()) {
        // Back-compat: one host, chosen by the global mode + transport config.
        pushHost(LogosTransportFactory::createHost(m_registryUrl), "(default)");
    } else {
        // One host per configured transport — lets a single provider serve
        // its object on several endpoints simultaneously (local-socket +
        // TCP, TCP + TCP+SSL, etc.).
        for (const auto& cfg : transports)
            pushHost(LogosTransportFactory::createHost(cfg, m_registryUrl), "(per-cfg)");
    }
}

LogosAPIProvider::~LogosAPIProvider()
{
    if (!m_registeredObjectName.isEmpty()) {
        // Defensive: m_transports should never contain nullptr (the
        // ctor filters them out via pushHost), but guard here too —
        // a future code path that pushes directly without going
        // through pushHost would otherwise crash on shutdown.
        for (auto& t : m_transports) {
            if (t) t->unpublishObject(m_registeredObjectName);
        }
    }
}

// QObject* path: auto-detects LogosProviderPlugin; falls back to QtProviderObject wrapper
bool LogosAPIProvider::registerObject(const QString& name, QObject* object)
{
    if (!object) {
        qWarning() << "LogosAPIProvider: Cannot register null object";
        return false;
    }

    if (name.isEmpty()) {
        qWarning() << "LogosAPIProvider: Cannot register object with empty name";
        return false;
    }

    if (m_moduleProxy) {
        qCritical() << "LogosAPIProvider: Object already registered. Only one registration per provider is allowed";
        return false;
    }

    // Check if this plugin implements LogosProviderPlugin (new API)
    LogosProviderPlugin* providerPlugin = qobject_cast<LogosProviderPlugin*>(object);
    if (providerPlugin) {
        qDebug() << "[LogosProviderObject] LogosAPIProvider: detected LogosProviderPlugin for" << name;
        LogosProviderObject* provider = providerPlugin->createProviderObject();
        if (provider) {
            return registerObject(name, provider);
        }
        qWarning() << "LogosAPIProvider: createProviderObject() returned null for" << name;
    }

    // Legacy path: wrap QObject in QtProviderObject adapter
    qDebug() << "[LogosProviderObject] LogosAPIProvider: wrapping QObject in QtProviderObject for" << name;

    m_qtProviderObject = new QtProviderObject(object, this);
    m_qtProviderObject->init(qobject_cast<LogosAPI*>(parent()));

    return publishProvider(name, m_qtProviderObject);
}

bool LogosAPIProvider::registerObject(const std::string& name, QObject* object)
{
    return registerObject(QString::fromStdString(name), object);
}

// New path: LogosProviderObject* -> ModuleProxy -> transport
bool LogosAPIProvider::registerObject(const QString& name, LogosProviderObject* provider)
{
    if (!provider) {
        qWarning() << "LogosAPIProvider: Cannot register null provider";
        return false;
    }

    if (name.isEmpty()) {
        qWarning() << "LogosAPIProvider: Cannot register provider with empty name";
        return false;
    }

    if (m_moduleProxy) {
        qCritical() << "LogosAPIProvider: Object already registered. Only one registration per provider is allowed";
        return false;
    }

    qDebug() << "[LogosProviderObject] LogosAPIProvider: registering LogosProviderObject directly for" << name;

    provider->init(qobject_cast<LogosAPI*>(parent()));

    return publishProvider(name, provider);
}

bool LogosAPIProvider::publishProvider(const QString& name, LogosProviderObject* provider)
{
    m_moduleProxy = new ModuleProxy(provider, this);

    // Publish on every configured transport. Success = any transport
    // accepted the publish (follow-up: surface per-transport failures).
    bool success = false;
    for (auto& t : m_transports) {
        if (!t) continue;  // see ~LogosAPIProvider — defensive null-skip.
        if (t->publishObject(name, m_moduleProxy)) success = true;
    }
    if (success) {
        m_registeredObjectName = name;
        qDebug() << "[LogosProviderObject] LogosAPIProvider: successfully published" << name;
    } else {
        qCritical() << "LogosAPIProvider: Failed to publish" << name;
    }

    return success;
}

QString LogosAPIProvider::registryUrl() const
{
    return m_registryUrl;
}

bool LogosAPIProvider::saveToken(const QString& from_module_name, const QString& token)
{
    if (!m_moduleProxy) {
        qWarning() << "LogosAPIProvider: Cannot save token - no module proxy available";
        return false;
    }

    qDebug() << "LogosAPIProvider: Delegating saveToken to module proxy for:" << from_module_name;
    return m_moduleProxy->saveToken(from_module_name, token);
}

void LogosAPIProvider::onEventResponse(LogosObject* object, const QString& eventName, const QVariantList& data)
{
    qDebug() << "[LogosObject] LogosAPIProvider::onEventResponse" << eventName << "-> LogosObject::emitEvent";

    if (eventName.isEmpty()) {
        qWarning() << "LogosAPIProvider: Event name cannot be empty";
        return;
    }
    if (!object) {
        qWarning() << "LogosAPIProvider: Cannot emit event on null object";
        return;
    }

    object->emitEvent(eventName, data);
}
