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
//   --backend consumer  the Qt-TYPED CONSUMER wrapper for a dependency /
//                       interface: <name>_api.{h,cpp}. Same public surface the
//                       legacy Qt wrapper had; the bodies convert at the edge
//                       and delegate to the lp path, so there is one transport,
//                       one codec and one Qt type mapper under both consumer
//                       surfaces instead of two parallel implementations.
//                       [--module <dep-name>] [--class <C>] [--bind static|bound]
//                       [--binding api|origin]
//                       --bind picks the call TARGET (baked vs runtime);
//                       --binding picks the call ORIGIN: `api` (default) keeps
//                       the LogosAPI-taking constructor, `origin` emits a
//                       wrapper with NO LogosAPI that is handed the consuming
//                       module's own name instead.
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
#include "lidl_gen_qt_consumer.h"

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
               "         --backend <qt|cdylib|ui|consumer>\n"
               "         [--impl-header <include-name>] [--output-dir <dir>]\n"
               "         [--bind static|bound] [--binding api|origin]\n";
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
        QString typeErr;
        if (!lidlCheckOptionalReturns(mod, &typeErr)) {
            err << typeErr << "\n";
            return 4;
        }
        outs.append({qs(mod.name) + "_qt_glue.h",
                     lidlMakeProviderHeader(mod, implClass, implHeader)});
        outs.append({qs(mod.name) + "_dispatch.cpp", lidlMakeProviderDispatch(mod)});
        if (!mod.events.empty())
            outs.append({qs(mod.name) + "_events.cpp",
                         lidlMakeEventsSource(mod, implClass, implHeader)});
    } else if (backend == "cdylib") {
        outs.append({qs(mod.name) + "_cdylib_glue.h", lidlMakeCdylibGlueHeader(mod, multi)});
        outs.append({qs(mod.name) + "_cdylib_glue.cpp", lidlMakeCdylibGlueSource(mod, multi)});
    } else if (backend == "consumer") {
        // The Qt-typed CONSUMER wrapper — `<name>_api.{h,cpp}`, the surface a
        // module calls its dependencies through. Same file names and same
        // public signatures as the wrapper logos-cpp-generator's
        // `--api-style qt` emitted; the bodies are a veneer over the lp path.
        //
        // --module names the call target (Static) / the files (Bound) and
        // defaults to the contract's own module name. --class defaults to its
        // PascalCase, matching the umbrella's `#include "<name>_api.h"`.
        QString depName = argValue(args, "--module");
        if (depName.isEmpty()) depName = qs(mod.name);
        QString cls = argValue(args, "--class");
        if (cls.isEmpty()) cls = lidlToPascalCase(depName);
        const QString bindArg = argValue(args, "--bind");
        if (!bindArg.isEmpty() && bindArg != "static" && bindArg != "bound") {
            err << "Unknown --bind: " << bindArg << " (expected static|bound)\n";
            return 2;
        }
        const QtConsumerBind bindMode =
            (bindArg == "bound") ? QtConsumerBind::Bound : QtConsumerBind::Static;

        // --binding api|origin. A separate axis from --bind: that one names the
        // call TARGET, this one names the call ORIGIN. `origin` emits the
        // LogosAPI-free wrapper whose constructor is handed the consuming
        // module's own name — the surface a cdylib module (no LogosAPI
        // anywhere) needs in order to hold Qt-typed dependency wrappers.
        //
        // An unrecognised value is REFUSED rather than defaulted: defaulting a
        // misspelt `--binding orgin` back to the LogosAPI form would emit a
        // wrapper the caller's umbrella cannot construct, and the failure would
        // land in generated code far from the typo.
        const QString bindingArg = argValue(args, "--binding");
        if (!bindingArg.isEmpty() && bindingArg != "api" && bindingArg != "origin") {
            err << "Unknown --binding: " << bindingArg << " (expected api|origin)\n";
            return 2;
        }
        const QtConsumerBinding bindingMode = (bindingArg == "origin")
                                                  ? QtConsumerBinding::ExplicitOrigin
                                                  : QtConsumerBinding::FromApi;

        QString recErr;
        if (!lidlCheckRecords(mod, &recErr)) {
            err << recErr << "\n";
            return 4;
        }
        // Same refusal as the provider path: a consumer that could CALL an
        // optional-returning method would have no way to tell the empty answer
        // from a failed call either.
        QString optErr;
        if (!lidlCheckOptionalReturns(mod, &optErr)) {
            err << optErr << "\n";
            return 4;
        }

        const QString headerRel = depName + "_api.h";
        outs.append({headerRel,
                     lidlMakeQtConsumerHeader(mod, depName, cls, bindMode, bindingMode)});
        outs.append({depName + "_api.cpp",
                     lidlMakeQtConsumerSource(mod, depName, cls, headerRel, bindMode,
                                              bindingMode)});
    } else {
        err << "Unknown --backend: " << backend << " (expected qt|cdylib|ui|consumer)\n";
        return 2;
    }

    const int rc = writeAll(outs, outputDir, out, err);
    out.flush();
    return rc;
}
