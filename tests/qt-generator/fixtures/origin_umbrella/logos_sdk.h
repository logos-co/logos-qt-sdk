#pragma once
#include <QString>
#include <string>
#include "plain_module_api.h"
#include "optional_module_api.h"

struct LogosModules {
    LogosModules() : plain_module(QStringLiteral("origin_probe_module")) {}
    PlainModule plain_module;
    OptionalModule bind_optional_module(const QString& moduleName) {
        return OptionalModule(QStringLiteral("origin_probe_module"), moduleName);
    }
    OptionalModule bind_optional_module(const std::string& moduleName) {
        return OptionalModule(QStringLiteral("origin_probe_module"), QString::fromStdString(moduleName));
    }
};
