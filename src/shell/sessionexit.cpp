#include "sessionexit.h"
#include "documentcollection.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QSocketNotifier>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

volatile sig_atomic_t SessionExit::s_writeFd = -1;

SessionExit::SessionExit(DocumentCollection *collection, QObject *parent)
    : QObject(parent), m_collection(collection)
{
}

SessionExit::~SessionExit()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        ::sigaction(SIGTERM, &m_previous, nullptr);
        s_writeFd = -1;
    }
    for (const int fd : m_pipe) if (fd >= 0) ::close(fd);
}

bool SessionExit::installSignalHandler()
{
    if (::pipe2(m_pipe, O_CLOEXEC | O_NONBLOCK) != 0) return false;
    struct sigaction action {};
    action.sa_handler = &SessionExit::signalHandler;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    s_writeFd = m_pipe[1];
    if (::sigaction(SIGTERM, &action, &m_previous) != 0) {
        s_writeFd = -1;
        return false;
    }
    m_notifier = new QSocketNotifier(m_pipe[0], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        m_notifier->setEnabled(false);
        terminate();
    });
    return true;
}

void SessionExit::signalHandler(int)
{
    // No Qt, allocation or file saving is async-signal-safe. A full pipe already
    // means termination is queued; preserve errno and let the GUI thread save.
    const int savedErrno = errno;
    const char byte = 1;
    ssize_t result;
    do { result = ::write(s_writeFd, &byte, 1); } while (result < 0 && errno == EINTR);
    errno = savedErrno;
}

void SessionExit::commitPending()
{
    if (!m_collection->flushForSessionEnd())
        qWarning("Session end: pending Markdown could not all be committed; %s",
                 qPrintable(m_collection->lastError()));
}

void SessionExit::terminate()
{
    if (m_terminating) return;
    m_terminating = true;
    commitPending();
    // exit() ends the loop without delivering another vetoable window close.
    // There is deliberately no aboutToQuit save: that would revive a discard.
    QCoreApplication::exit(0);
}

bool SessionExit::closeForSessionEnd()
{
    if (m_terminating) return true;
    // Qt Wayland does not expose a session-manager callback. Plasma sends the
    // same xdg close as the titlebar, while KSMServer remains in shutdown state.
    // A missing/unresponsive service must leave ordinary close semantics intact.
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.ksmserver"),
        QStringLiteral("/KSMServer"), QStringLiteral("org.kde.KSMServerInterface"),
        QStringLiteral("isShuttingDown"));
    const QDBusReply<bool> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 250);
    if (!reply.isValid() || !reply.value()) return false;
    terminate();
    return true;
}
