#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace FanFold {
inline bool safeExistingIcon(const QString &libraryRoot, const QString &relative)
{
    const QRegularExpression allowed(QStringLiteral("^Assets/icons/[A-Za-z0-9_-]+\\.(?:svg|png)$"));
    if (!allowed.match(relative).hasMatch()) return false;
#ifndef Q_OS_UNIX
    Q_UNUSED(libraryRoot)
    return false;
#else
    const QByteArray rootName = QFile::encodeName(libraryRoot);
    int fd = ::open(rootName.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return false;
    for (const QByteArray &part : {QByteArray("Assets"), QByteArray("icons"),
                                   QFile::encodeName(relative.mid(QStringLiteral("Assets/icons/").size()))}) {
        const bool last = part.contains('.');
        const int next = ::openat(fd, part.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC
                                                          | (last ? O_NONBLOCK : O_DIRECTORY));
        ::close(fd);
        if (next < 0) return false;
        fd = next;
    }
    struct stat st {};
    const bool regular = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
    ::close(fd);
    return regular;
#endif
}

// Keep lookups/writes relative to held directory handles: pre-existing symlinks and
// replacements with symlinks are not followed. This does not prevent a process with
// permission to rename a parent directory from moving an already-open directory
// outside the library while a write is in progress.
inline QString exportIconBytes(const QString &libraryRoot, const QString &noteId,
                               const QByteArray &bytes, const QString &extension)
{
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$"))
             .match(noteId).hasMatch() || bytes.isEmpty()
        || (extension != QStringLiteral("svg") && extension != QStringLiteral("png"))) return {};
#ifndef Q_OS_UNIX
    Q_UNUSED(libraryRoot)
    return {}; // no race-safe directory-handle implementation on this platform
#else
    const QByteArray rootName = QFile::encodeName(libraryRoot);
    const int root = ::open(rootName.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) return {};
    auto child = [](int parent, const char *name) {
        if (::mkdirat(parent, name, 0700) != 0 && errno != EEXIST) return -1;
        return ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    };
    const int assets = child(root, "Assets");
    const int icons = assets >= 0 ? child(assets, "icons") : -1;
    QString result;
    if (icons >= 0) {
        for (int index = 0; index < 10000; ++index) {
            const QString leaf = QStringLiteral("fanfold-icon-%1%2.%3")
                .arg(noteId, index ? QStringLiteral("-%1").arg(index) : QString(), extension);
            const QByteArray name = QFile::encodeName(leaf);
            const int old = ::openat(icons, name.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
            if (old >= 0) {
                struct stat st {};
                QByteArray existing;
                if (::fstat(old, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == bytes.size()) {
                    existing.resize(bytes.size());
                    qsizetype readCount = 0;
                    while (readCount < existing.size()) {
                        const ssize_t n = ::read(old, existing.data() + readCount,
                                                 static_cast<size_t>(existing.size() - readCount));
                        if (n <= 0) break;
                        readCount += n;
                    }
                    if (readCount == existing.size() && existing == bytes)
                        result = QStringLiteral("Assets/icons/") + leaf;
                }
                ::close(old);
                if (!result.isEmpty()) break;
                continue;
            }
            if (errno != ENOENT) continue; // a symlink or inaccessible entry is never reused
            const int fd = ::openat(icons, name.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (fd < 0) { if (errno == EEXIST) continue; break; }
            qsizetype written = 0;
            while (written < bytes.size()) {
                const ssize_t n = ::write(fd, bytes.constData() + written,
                                          static_cast<size_t>(bytes.size() - written));
                if (n <= 0) break;
                written += n;
            }
            const bool okay = written == bytes.size() && ::fsync(fd) == 0;
            ::close(fd);
            if (okay) result = QStringLiteral("Assets/icons/") + leaf;
            else ::unlinkat(icons, name.constData(), 0);
            break;
        }
        ::close(icons);
    }
    if (assets >= 0) ::close(assets);
    ::close(root);
    return result;
#endif
}
} // namespace FanFold
