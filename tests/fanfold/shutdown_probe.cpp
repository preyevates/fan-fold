#include <QApplication>
#include <QFile>
#include <QDir>
#include <QDBusConnection>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QSessionManager>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <sys/stat.h>
#include "documentcollection.h"

#undef main
int fanfoldApplicationMain(int argc, char **argv);

class FakeKsm final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KSMServerInterface")
public slots:
    bool isShuttingDown() const { return !qgetenv("FANFOLD_PROBE").startsWith("normal"); }
};

static void scheduleProbe()
{
    const QByteArray scenario = qgetenv("FANFOLD_PROBE");
    QTimer::singleShot(1200, qApp, [scenario] {
        auto *ksm = new FakeKsm;
        ksm->setParent(qApp);
        if (!QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.ksmserver"))
            || !QDBusConnection::sessionBus().registerObject(QStringLiteral("/KSMServer"),
                ksm, QDBusConnection::ExportAllSlots)) qFatal("private KSM mock failed");
        for (QWindow *window : QGuiApplication::topLevelWindows()) {
            QQmlContext *context = QQmlEngine::contextForObject(window);
            if (!context) continue;
            auto *collection = qobject_cast<DocumentCollection *>(
                context->contextProperty(QStringLiteral("collection")).value<QObject *>());
            if (!collection) continue;
            window->show();
            QCoreApplication::processEvents();
            std::fprintf(stderr, "PROBE window visible=%d handle=%d\n", window->isVisible(), window->handle() != nullptr);
            const QString id = collection->catalogIds().value(0);
            QWindow *closeTarget = window;
            if (scenario.startsWith("pinned")) {
                if (!collection->setPinned(id, true)) qFatal("could not pin fixture");
                if (!QMetaObject::invokeMethod(window, "restorePersistedPins")) qFatal("could not restore pin");
                closeTarget = nullptr;
                for (QWindow *candidate : QGuiApplication::topLevelWindows()) {
                    if (candidate->property("documentId").toString() == id) closeTarget = candidate;
                }
                if (!closeTarget) qFatal("real pinned window not found");
                closeTarget->show();
                QCoreApplication::processEvents();
            }
            if (scenario != "quit") {
                if (!collection->updateContent(id, QStringLiteral("pending shutdown fixture\n")))
                    qFatal("could not stage fixture");
                if (scenario.endsWith("-failure")) {
                    const QDir recovery(collection->rootPath() + QStringLiteral("/.fanfold/recovery"));
                    for (const QString &file : (scenario.startsWith("normal") ? QStringList() : recovery.entryList(QDir::Files)))
                        if (!QFile::remove(recovery.filePath(file))) qFatal("could not remove fixture journal");
                    const QByteArray root = QFile::encodeName(collection->rootPath());
                    if (::chmod(root.constData(), 0500) != 0) qFatal("chmod failed");
                }
            }
            std::fprintf(stderr, "PROBE ready scenario=%s nativeDirty=%d\n", scenario.constData(),
                         collection->document(id)->dirty());
            QTimer::singleShot(2500, qApp, [] {
                std::fprintf(stderr, "PROBE FAILED: quit vetoed / deadline expired\n");
                QCoreApplication::exit(42);
            });
            const auto started = std::chrono::steady_clock::now();
            QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [started] {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                std::fprintf(stderr, "PROBE termination latency=%lldms\n", static_cast<long long>(elapsed));
            });
            if (scenario.startsWith("term")) ::raise(SIGTERM);
            else if (scenario.startsWith("session") || scenario == "state") {
                auto *manager = qApp->findChild<QSessionManager *>();
                if (!manager) qFatal("Qt session manager not found");
                if (scenario == "state") qGuiApp->saveStateRequest(*manager);
                else qGuiApp->commitDataRequest(*manager);
                // XSMP's Die callback asks Qt to quit after SaveYourselfDone.
                QCoreApplication::quit();
            } else if (scenario.startsWith("normal")) {
                const bool accepted = window->close();
                if (accepted || !window->property("closeSaveError").toString().contains(QStringLiteral("Atomic autosave")))
                    qFatal("normal close did not refuse failed save");
                std::fprintf(stderr, "PROBE normal close refused with native error\n");
                QCoreApplication::exit(0);
            } else if (scenario.startsWith("wayland") || scenario.startsWith("pinned")) {
                window->setProperty("dirty", true);
                std::fprintf(stderr, "PROBE compositor close accepted=%d\n", closeTarget->close());
            } else QMetaObject::invokeMethod(window, "requestClose");
            return;
        }
        qFatal("real shell root not found");
    });
}
Q_COREAPP_STARTUP_FUNCTION(scheduleProbe)

int main(int argc, char **argv)
{
    return fanfoldApplicationMain(argc, argv);
}
#include "shutdown_probe.moc"
