#ifndef SAMPLE_PROVIDER_H
#define SAMPLE_PROVIDER_H

#include "logos_provider_object.h"
#include <QString>
#include <QStringList>
#include <QJsonArray>

/**
 * @brief Sample provider that exercises the LOGOS_PROVIDER + LOGOS_METHOD pipeline.
 *
 * This mimics real-world module patterns (like logos-package-manager-module)
 * to verify that the code generator + SDK dispatch chain works correctly.
 * The generated logos_provider_dispatch.cpp provides callMethod() and getMethods().
 */
class SampleProvider : public LogosProviderBase {
    LOGOS_PROVIDER(SampleProvider, "sample_provider", "2.0.0")

public:
    // -- Various return types --

    LOGOS_METHOD QString echo(const QString& message);
    LOGOS_METHOD bool installPlugin(const QString& pluginPath, bool skipIfNotNewerVersion);
    LOGOS_METHOD QJsonArray getPackages();
    LOGOS_METHOD QJsonArray getPackages(const QString& category);
    LOGOS_METHOD QStringList getCategories();
    LOGOS_METHOD QStringList resolveDependencies(const QStringList& packageNames);
    LOGOS_METHOD int add(int a, int b);

    // -- void return type --

    LOGOS_METHOD void setDirectory(const QString& directory);
    LOGOS_METHOD void testEvent(const QString& message);

    // -- State for verification --

    QString lastEcho;
    QString lastPluginPath;
    bool lastSkipFlag = false;
    QString lastCategory;
    QStringList lastDependencies;
    int lastA = 0, lastB = 0;
    QString lastDirectory;
    QString lastEventMessage;
};

#endif // SAMPLE_PROVIDER_H
