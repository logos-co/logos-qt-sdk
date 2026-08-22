// logos-qt-generator — ALL Qt glue emission for Logos modules.
//
// The Qt-confinement invariant puts generated Qt code in the Qt layer: this
// tool (hosted by logos-qt-sdk) emits the Qt CONSUMER glue, while
// logos-cpp-sdk's logos-cpp-generator keeps the Qt-free
// outputs (std typed wrappers, the logos_sdk umbrella, cdylib impl-exports,
// LIDL derivation). Both tools share one LIDL frontend: the sources under
// logos-cpp-sdk's share/lidl-frontend/, compiled into this binary.
//
// Input is either --from-header <impl.h> --impl-class <C> --metadata <m.json>
// (the contract derived from the C++ class) or --lidl <contract.lidl> (the
// committed contract — e.g. Rust cdylib modules).
//
// There is deliberately NO backend that wraps a module implementation directly
// in a Qt provider object. A module is a plain shared library; making one a Qt
// plugin is a downstream HOSTING step, and it happens over the language-neutral
// module-impl C ABI:
//
//   plain std impl
//     -> logos-cpp-sdk  lidl_gen_cdylib      -> logos_module_* C ABI
//     -> logos-plugin-qt qt-host-generator   -> <name>CdylibProvider
//
// The retired `--backend qt` short-circuited that seam — it consumed an impl
// class and emitted a Qt provider for it, which is the one thing that made a
// module NOT language-neutral. `--backend cdylib` is retired too, for a
// narrower reason: what it emitted was the HOSTING half — Qt glue over the C
// ABI — and that half belongs with the host, so it lives in logos-plugin-qt's
// qt-host-generator now. `--backend ui` is retired too: the view plugin glue
// moved to logos-view-module's logos-view-generator, to sit with the
// LogosView*.in templates its output compiles against and the
// logos_ui_plugin_context.h its output calls into. What remains here is the
// CONSUMER half. Modes:
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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <QJsonDocument>
#include <QJsonObject>

#include "impl_header_parser.h"
#include "lidl_emit_common.h"
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
    QString outputDir        = argValue(args, "--output-dir");
    QString implHeader       = argValue(args, "--impl-header");

    // Retired backends are refused BEFORE the input-shape usage gate below.
    // Ordering is the whole point: a caller still passing `--backend ui` is
    // passing --metadata/--rep and no --lidl, so the gate would fire first and
    // answer with a generic parse error -- burying the one sentence that says
    // where the work went under a usage string. Refuse by NAME, first.
    if (backend == "ui") {
        // RETIRED, and refused loudly for the same reason `qt` and `cdylib`
        // below are: the work moved to a different BINARY, so it cannot be
        // defaulted or aliased.
        //
        // This one gets its own message because its failure mode was the
        // nastiest of the three. The ui emitter existed HERE and in
        // logos-view-module simultaneously, byte-identical at first, and then
        // this copy gained the module teardown hook (logos-qt-sdk#38) while the
        // other did not. Nothing detected the divergence, because a generated
        // view plugin that omits the hook is not a broken build: ui-host
        // reaches aboutToUnload() BY NAME through the meta-object, so a class
        // that never declares it simply has no such meta-method,
        // QMetaObject::invokeMethod returns false, and the host moves on --
        // indistinguishable from a view answering "Synchronous, nothing to wait
        // for". Every view would silently lose its chance to finish.
        //
        // The emitter now lives in logos-view-module ONLY, beside the
        // LogosView*.in templates its output is compiled against and beside
        // logos_ui_plugin_context.h, which its output calls into. Those three
        // are one authoring surface; keeping the emitter here kept it pinned
        // separately from the header it must agree with.
        err << "Error: --backend ui was removed from logos-qt-generator; the "
               "view plugin glue is emitted by logos-view-module now.\n"
               "  Emit it with:  logos-view-generator --backend ui "
               "--metadata <metadata.json> --rep <view.rep>\n"
               "                 [--backend-class <C>] [--backend-header <h>] "
               "[--output-dir <dir>]\n"
               "logos-module-builder does this for `type: \"ui_qml\"` + "
               "`interface: \"universal\"`. If a BUILD produced this, that "
               "builder predates the repoint and is the thing to update.\n";
        return 2;
    }
    if (backend == "qt" || backend == "cdylib") {
        // Both RETIRED, and refused loudly rather than silently ignored. `qt`
        // wrapped an impl class in a Qt provider, skipping the language-neutral
        // seam outright. `cdylib` respected the seam but emitted the HOSTING
        // half of it, which now lives with the host in logos-plugin-qt.
        //
        // Neither is a flag rename, so neither can be defaulted: the work moved
        // to a different BINARY. A caller landing here is in practice a
        // logos-module-builder predating the repoint of cdylib codegen onto
        // logos-qt-host-generator — say so, because the symptom otherwise
        // surfaces as a cmake error about missing generated sources, far from
        // the pin that actually needs moving.
        err << "Error: --backend " << backend << " was removed from "
               "logos-qt-generator; this tool emits the CONSUMER glue, not the "
               "Qt-plugin (provider) glue.\n"
               "  Emit the C ABI:  logos-cpp-generator --lidl <c> --backend cdylib "
               "--impl-class <C> --impl-header <h>\n"
               "  Host it in Qt:   logos-qt-host-generator --lidl <c> --backend cdylib\n"
               "logos-module-builder does both for `interface: \"universal\"` and "
               "`interface: \"cdylib\"`. If a BUILD produced this, that builder "
               "predates the repoint and is the thing to update.\n";
        return 2;
    }

    const bool fromHeader = !headerPath.isEmpty();
    if ((!fromHeader && lidlPath.isEmpty()) || backend.isEmpty()
        || (fromHeader && (implClass.isEmpty() || metadata.isEmpty()))) {
        err << "Usage: logos-qt-generator (--from-header <impl.h> --impl-class <C>\n"
               "         --metadata <metadata.json> | --lidl <contract.lidl>)\n"
               "         --backend consumer\n"
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

    {
        // Every module answers name()/version(), so every consumer wrapper
        // offers them. Added here rather than read from the contract: the
        // .lidl carries only what the author wrote, and the provider adds the
        // same two methods from the same frontend function.
        QString idErr;
        if (!lidlInjectIdentity(mod, &idErr)) {
            err << (fromHeader ? headerPath : lidlPath) << ": " << idErr << "\n";
            return 4;
        }
    }

    QList<Out> outs;
    if (backend == "consumer") {
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
        const QString headerRel = depName + "_api.h";
        outs.append({headerRel,
                     lidlMakeQtConsumerHeader(mod, depName, cls, bindMode, bindingMode)});
        outs.append({depName + "_api.cpp",
                     lidlMakeQtConsumerSource(mod, depName, cls, headerRel, bindMode,
                                              bindingMode)});
    } else {
        err << "Unknown --backend: " << backend << " (expected consumer)\n";
        return 2;
    }

    const int rc = writeAll(outs, outputDir, out, err);
    out.flush();
    return rc;
}
