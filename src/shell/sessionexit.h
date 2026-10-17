#pragma once

#include <QObject>
#include <csignal>

class DocumentCollection;
class QSocketNotifier;

// System termination must not enter an interactive save/discard prompt.
class SessionExit final : public QObject
{
    Q_OBJECT
public:
    explicit SessionExit(DocumentCollection *collection, QObject *parent = nullptr);
    ~SessionExit() override;
    bool installSignalHandler();
    Q_INVOKABLE bool closeForSessionEnd();
    void commitPending();
private:
    void terminate();
    static void signalHandler(int signal);
    static volatile sig_atomic_t s_writeFd;
    DocumentCollection *m_collection;
    int m_pipe[2] = {-1, -1};
    struct sigaction m_previous {};
    QSocketNotifier *m_notifier = nullptr;
    bool m_terminating = false;
};
