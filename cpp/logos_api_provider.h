#ifndef LOGOS_API_PROVIDER_H
#define LOGOS_API_PROVIDER_H

#include "logos_transport_config.h"

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QMap>
#include <memory>
#include <string>

class LogosTransportHost;
class LogosObject;
class ModuleProxy;
class LogosProviderObject;
class QtProviderObject;

/**
 * @brief LogosAPIProvider handles registering objects for access by consumers
 * 
 * Supports two registration paths:
 *   1. registerObject(name, QObject*)           — wraps in QtProviderObject, then ModuleProxy
 *   2. registerObject(name, LogosProviderObject*) — wraps directly in ModuleProxy
 * Both paths converge at ModuleProxy -> transport.
 */
class LogosAPIProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @param module_name  The module this provider belongs to.
     * @param transports   Optional per-instance transport override. When empty,
     *                     the provider uses the process-global default. This
     *                     is what lets a daemon expose `core_service` on
     *                     TCP/TLS while keeping module-to-module traffic on
     *                     the local-socket default.
     */
    explicit LogosAPIProvider(const QString& module_name,
                              LogosTransportSet transports = {},
                              QObject *parent = nullptr);
    // Back-compat overload
    LogosAPIProvider(const QString& module_name, QObject *parent)
        : LogosAPIProvider(module_name, LogosTransportSet{}, parent) {}
    ~LogosAPIProvider();

    /**
     * @brief Register a legacy QObject-based plugin.
     * Wraps in QtProviderObject, then ModuleProxy.
     */
    bool registerObject(const QString& name, QObject* object);

    /**
     * @brief Register a legacy QObject-based plugin — const char* overload (resolves ambiguity)
     */
    bool registerObject(const char* name, QObject* object)
        { return registerObject(QString(name), object); }

    /**
     * @brief Register a legacy QObject-based plugin (std::string overload).
     */
    bool registerObject(const std::string& name, QObject* object);

    /**
     * @brief Register a new-API LogosProviderObject plugin.
     * Wraps directly in ModuleProxy.
     */
    bool registerObject(const QString& name, LogosProviderObject* provider);

    QString registryUrl() const;
    bool saveToken(const QString& from_module_name, const QString& token);

public slots:
    void onEventResponse(LogosObject* object, const QString& eventName, const QVariantList& data);

private:
    bool publishProvider(const QString& name, LogosProviderObject* provider);

    // One host per configured transport. Single-entry vector for back-compat.
    std::vector<std::unique_ptr<LogosTransportHost>> m_transports;
    QString m_registryUrl;
    QMap<QString, QString> m_tokens;
    ModuleProxy* m_moduleProxy;
    QtProviderObject* m_qtProviderObject;
    QString m_registeredObjectName;
};

#endif // LOGOS_API_PROVIDER_H
