#include "sample_provider.h"
#include <QJsonObject>

QString SampleProvider::echo(const QString& message)
{
    lastEcho = message;
    return message;
}

bool SampleProvider::installPlugin(const QString& pluginPath, bool skipIfNotNewerVersion)
{
    lastPluginPath = pluginPath;
    lastSkipFlag = skipIfNotNewerVersion;
    return true;
}

QJsonArray SampleProvider::getPackages()
{
    QJsonArray arr;
    arr.append(QJsonObject{{"name", "pkg_a"}});
    arr.append(QJsonObject{{"name", "pkg_b"}});
    return arr;
}

QJsonArray SampleProvider::getPackages(const QString& category)
{
    lastCategory = category;
    QJsonArray arr;
    arr.append(QJsonObject{{"name", "pkg_filtered"}, {"category", category}});
    return arr;
}

QStringList SampleProvider::getCategories()
{
    return QStringList{"network", "storage", "ui"};
}

QStringList SampleProvider::resolveDependencies(const QStringList& packageNames)
{
    lastDependencies = packageNames;
    QStringList resolved = packageNames;
    resolved.append("common_dep");
    return resolved;
}

int SampleProvider::add(int a, int b)
{
    lastA = a;
    lastB = b;
    return a + b;
}

void SampleProvider::setDirectory(const QString& directory)
{
    lastDirectory = directory;
}

void SampleProvider::testEvent(const QString& message)
{
    lastEventMessage = message;
    emitEvent("testEvent", {QVariant(message)});
}
