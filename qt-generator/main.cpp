// logos-qt-generator — ALL Qt glue emission for Logos modules.
//
// The Qt-confinement invariant puts generated Qt code in the Qt layer: this
// tool (hosted by logos-qt-sdk) emits the Qt-plugin glue for every module
// flavor, while logos-cpp-sdk's logos-cpp-generator keeps the Qt-free
// outputs (std typed wrappers, the logos_sdk umbrella, cdylib impl-exports,
// LIDL derivation). Both tools share one LIDL frontend: the sources under
// logos-cpp-sdk's share/lidl-frontend/, compiled into this binary.
//
// Input is either --from-header <impl.h> --impl-class <C> --metadata <m.json>
// (the contract derived from the C++ class) or --lidl <contract.lidl> (the
// committed contract — e.g. Rust cdylib modules). Modes:
//   --backend qt      universal C++ module glue:
//                       <name>_qt_glue.h, <name>_dispatch.cpp,
//                       <name>_events.cpp (when logos_events: present)
//   --backend cdylib  the uniform Qt glue over the module-impl C ABI:
//                       <name>_cdylib_glue.{h,cpp}
//                       (the C-ABI impl-exports come from logos-cpp-generator)
//   --backend ui      UI plugin backend (type=ui_qml + interface=universal):
//                       --metadata <m.json> --rep <view.rep>
//                       [--backend-class C] [--backend-header h]
//                       emits <name>_ui_interface.h + <name>_ui_glue.{h,cpp}
//                       around the USER-written .rep + *Backend class

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <QJsonDocument>
#include <QJsonObject>

#include "impl_header_parser.h"
#include "lidl_emit_common.h"
#include "lidl_gen_provider.h"
#include "lidl_gen_cdylib_glue.h"
#include "lidl_gen_ui.h"

namespace {

struct Out { QString file; QString content; };

int writeAll(const QList<Out>& outs, const QString& dir,
             QTextStream& out, QTextStream& err)
{
    for (const Out& o : outs) {
        const QString abs = QDir(dir).filePath(o.file);
        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            err << "Failed to write: " << abs << "\n";
            return 1;
        }
        f.write(o.content.toUtf8());
        out << "Generated: " << abs << "\n";
    }
    return 0;
}

QString argValue(const QStringList& args, const QString& flag)
{
    const int i = args.indexOf(flag);
    return (i != -1 && i + 1 < args.size()) ? args.at(i + 1) : QString();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);
    const QStringList args = app.arguments();

    const QString headerPath = argValue(args, "--from-header");
    const QString lidlPath   = argValue(args, "--lidl");
    const QString implClass  = argValue(args, "--impl-class");
    const QString metadata   = argValue(args, "--metadata");
    const QString backend    = argValue(args, "--backend");
    // concurrency:"multi" (from metadata.json, fed by the builder) ⇒ also emit the
    // concurrent-dispatch glue (callMethodAsync over logos_module_dispatch_async).
    const bool multi = argValue(args, "--concurrency") == QStringLiteral("multi");
    QString outputDir        = argValue(args, "--output-dir");
    QString implHeader       = argValue(args, "--impl-header");

    // --backend ui: standalone mode — the user writes the .rep and the
    // Backend class; only the Plugin/Interface pair is generated.
    if (backend == "ui") {
        const QString repPath = argValue(args, "--rep");
        if (metadata.isEmpty() || repPath.isEmpty()) {
            err << "Usage: logos-qt-generator --backend ui --metadata <metadata.json>\n"
                   "         --rep <view.rep> [--backend-class <C>]\n"
                   "         [--backend-header <include-name>] [--output-dir <dir>]\n";
            return 1;
        }
        QFile mf(metadata);
        if (!mf.open(QIODevice::ReadOnly)) {
            err << "Failed to read metadata: " << metadata << "\n";
            return 3;
        }
        const QJsonObject meta = QJsonDocument::fromJson(mf.readAll()).object();
        UiGlueSpec spec;
        spec.moduleName = meta.value(QStringLiteral("name")).toString();
        spec.moduleVersion = meta.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0"));
        if (spec.moduleName.isEmpty()) {
            err << "metadata.json has no name\n";
            return 3;
        }
        spec.pluginBase = lidlToPascalCase(spec.moduleName);
        QString repErr;
        if (!lidlUiParseRepClass(repPath, &spec.repClass, &repErr)) {
            err << "Error: " << repErr << "\n";
            return 4;
        }
        spec.backendClass = argValue(args, "--backend-class");
        if (spec.backendClass.isEmpty())
            spec.backendClass = spec.pluginBase + QStringLiteral("Backend");
        spec.backendHeader = argValue(args, "--backend-header");
        if (spec.backendHeader.isEmpty())
            spec.backendHeader = spec.moduleName + QStringLiteral("_backend.h");
        if (outputDir.isEmpty())
            outputDir = QDir::current().filePath("generated");
        QDir().mkpath(outputDir);
        QList<Out> outs;
        outs.append({spec.moduleName + "_ui_interface.h", lidlMakeUiInterfaceHeader(spec)});
        outs.append({spec.moduleName + "_ui_glue.h", lidlMakeUiGlueHeader(spec)});
        outs.append({spec.moduleName + "_ui_glue.cpp", lidlMakeUiGlueSource(spec)});
        const int rc = writeAll(outs, outputDir, out, err);
        out.flush();
        return rc;
    }

    const bool fromHeader = !headerPath.isEmpty();
    if ((!fromHeader && lidlPath.isEmpty()) || backend.isEmpty()
        || (fromHeader && (implClass.isEmpty() || metadata.isEmpty()))) {
        err << "Usage: logos-qt-generator (--from-header <impl.h> --impl-class <C>\n"
               "         --metadata <metadata.json> | --lidl <contract.lidl>)\n"
               "         --backend <qt|cdylib|ui>\n"
               "         [--impl-header <include-name>] [--output-dir <dir>]\n";
        return 1;
    }
    if (implHeader.isEmpty() && fromHeader)
        implHeader = QFileInfo(headerPath).fileName();
    if (outputDir.isEmpty())
        outputDir = QDir::current().filePath("generated");
    QDir().mkpath(outputDir);

    ModuleDecl mod;
    if (fromHeader) {
        ImplParseResult pr = parseImplHeader(headerPath, implClass, metadata, err);
        if (pr.hasError()) {
            err << "Error parsing impl header: " << pr.error << "\n";
            return 4;
        }
        mod = pr.module;
    } else {
        QFile f(lidlPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            err << "Failed to open LIDL file: " << lidlPath << "\n";
            return 3;
        }
        LidlParseResult pr = lidlParse(QString::fromUtf8(f.readAll()));
        if (pr.hasError()) {
            err << lidlPath << ":" << pr.errorLine << ":" << pr.errorColumn
                << ": " << pr.error << "\n";
            return 4;
        }
        mod = pr.module;
    }

    QList<Out> outs;
    if (backend == "qt") {
        outs.append({qs(mod.name) + "_qt_glue.h",
                     lidlMakeProviderHeader(mod, implClass, implHeader)});
        outs.append({qs(mod.name) + "_dispatch.cpp", lidlMakeProviderDispatch(mod)});
        if (!mod.events.empty())
            outs.append({qs(mod.name) + "_events.cpp",
                         lidlMakeEventsSource(mod, implClass, implHeader)});
    } else if (backend == "cdylib") {
        outs.append({qs(mod.name) + "_cdylib_glue.h", lidlMakeCdylibGlueHeader(mod, multi)});
        outs.append({qs(mod.name) + "_cdylib_glue.cpp", lidlMakeCdylibGlueSource(mod, multi)});
    } else {
        err << "Unknown --backend: " << backend << " (expected qt|cdylib|ui)\n";
        return 2;
    }

    const int rc = writeAll(outs, outputDir, out, err);
    out.flush();
    return rc;
}
