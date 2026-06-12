// logos-qt-generator — ALL Qt glue emission for Logos modules.
//
// The Qt-confinement invariant puts generated Qt code in the Qt layer: this
// tool (hosted by logos-qt-sdk) emits the Qt-plugin glue for every module
// flavor, while logos-cpp-sdk's logos-cpp-generator keeps the Qt-free
// outputs (std typed wrappers, the logos_sdk umbrella, cdylib impl-exports,
// LIDL derivation). Both tools share one LIDL frontend: the sources under
// logos-cpp-sdk's share/lidl-frontend/, compiled into this binary.
//
// Modes (all --from-header <impl.h> --impl-class <C> --metadata <m.json>):
//   --backend qt      universal C++ module glue:
//                       <name>_qt_glue.h, <name>_dispatch.cpp,
//                       <name>_events.cpp (when logos_events: present)
//   --backend cdylib  the uniform Qt glue over the module-impl C ABI:
//                       <name>_cdylib_glue.{h,cpp}
//                       (the C-ABI impl-exports come from logos-cpp-generator)
//   --backend ui      UI plugin backend (type=ui_qml + interface=universal):
//                       <name>.rep, <name>_ui_interface.h,
//                       <name>_ui_glue.{h,cpp}

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "impl_header_parser.h"
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
    const QString implClass  = argValue(args, "--impl-class");
    const QString metadata   = argValue(args, "--metadata");
    const QString backend    = argValue(args, "--backend");
    QString outputDir        = argValue(args, "--output-dir");
    QString implHeader       = argValue(args, "--impl-header");

    if (headerPath.isEmpty() || implClass.isEmpty() || metadata.isEmpty()
        || backend.isEmpty()) {
        err << "Usage: logos-qt-generator --from-header <impl.h> --impl-class <C>\n"
               "         --metadata <metadata.json> --backend <qt|cdylib|ui>\n"
               "         [--impl-header <include-name>] [--output-dir <dir>]\n";
        return 1;
    }
    if (implHeader.isEmpty())
        implHeader = QFileInfo(headerPath).fileName();
    if (outputDir.isEmpty())
        outputDir = QDir::current().filePath("generated");
    QDir().mkpath(outputDir);

    ImplParseResult pr = parseImplHeader(headerPath, implClass, metadata, err);
    if (pr.hasError()) {
        err << "Error parsing impl header: " << pr.error << "\n";
        return 4;
    }
    const ModuleDecl& mod = pr.module;

    QList<Out> outs;
    if (backend == "qt") {
        outs.append({mod.name + "_qt_glue.h",
                     lidlMakeProviderHeader(mod, implClass, implHeader)});
        outs.append({mod.name + "_dispatch.cpp", lidlMakeProviderDispatch(mod)});
        if (!mod.events.isEmpty())
            outs.append({mod.name + "_events.cpp",
                         lidlMakeEventsSource(mod, implClass, implHeader)});
    } else if (backend == "cdylib") {
        outs.append({mod.name + "_cdylib_glue.h", lidlMakeCdylibGlueHeader(mod)});
        outs.append({mod.name + "_cdylib_glue.cpp", lidlMakeCdylibGlueSource(mod)});
    } else if (backend == "ui") {
        QString uiErr;
        if (!lidlUiSupported(mod, &uiErr)) {
            err << "Error: module not ui-backend-eligible: " << uiErr << "\n";
            return 12;
        }
        outs.append({mod.name + ".rep", lidlMakeUiRepFile(mod)});
        outs.append({mod.name + "_ui_interface.h", lidlMakeUiInterfaceHeader(mod)});
        outs.append({mod.name + "_ui_glue.h",
                     lidlMakeUiGlueHeader(mod, implClass, implHeader)});
        outs.append({mod.name + "_ui_glue.cpp",
                     lidlMakeUiGlueSource(mod, implClass)});
    } else {
        err << "Unknown --backend: " << backend << " (expected qt|cdylib|ui)\n";
        return 2;
    }

    const int rc = writeAll(outs, outputDir, out, err);
    out.flush();
    return rc;
}
