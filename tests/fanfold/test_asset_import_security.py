"""Exercise ShellControl::importAsset against disposable filesystem fixtures.

Run: python3 -B tests/fanfold/test_asset_import_security.py
The harness compiles the actual method body from main.cpp against a tiny collection
stand-in, avoiding the KDE/QML shell and any real notes folder.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[2] / "src/shell/main.cpp"
START = "    Q_INVOKABLE QVariantMap importAsset("
END = "    /**\n     * Every application installed"


def main():
    source = SOURCE.read_text(encoding="utf-8")
    assert source.count(START) == 1 and source.count(END) == 1
    method = source[source.index(START):source.index(END)]
    assert '#ifndef Q_OS_UNIX' in method and 'requires race-safe directory handles' in method, "non-Unix import must fail closed"
    harness = r'''
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Linker-level fault injection exercises the real openat/write/cleanup path.
// Only writes to the opened asset descriptor are intercepted.
static QByteArray injectedPath;
static QByteArray movedPartialPath;
static int injectedWrites = 0;
static bool injectWriteFailure = false;
extern "C" ssize_t __real_write(int, const void *, size_t);
extern "C" ssize_t __wrap_write(int fd, const void *data, size_t length) {
    char target[4096];
    const QByteArray descriptor = QByteArray("/proc/self/fd/") + QByteArray::number(fd);
    const ssize_t size = ::readlink(descriptor.constData(), target, sizeof(target) - 1);
    if (!injectWriteFailure || size < 0) return __real_write(fd, data, length);
    target[size] = '\0';
    if (injectedPath != target) return __real_write(fd, data, length);
    ++injectedWrites;
    if (injectedWrites == 1) return __real_write(fd, data, length > 3 ? 3 : length);
    if (!movedPartialPath.isEmpty()) {
        if (::rename(injectedPath.constData(), movedPartialPath.constData()) != 0) std::abort();
        const int replacement = ::open(injectedPath.constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (replacement < 0 || __real_write(replacement, "replacement", 11) != 11) std::abort();
        ::close(replacement);
    }
    errno = EIO;
    return -1;
}

struct Collection {
    QString path;
    QString rootPath() const { return path; }
};
class ShellControl {
public:
    Collection *m_collection;
''' + method + r'''
};

static void require(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
static QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
int main() {
    QTemporaryDir sandbox;
    require(sandbox.isValid(), "temporary sandbox");
    const QDir top(sandbox.path());
    require(top.mkpath("library") && top.mkpath("outside"), "test directories");
    const QString library = top.filePath("library");
    const QString outside = top.filePath("outside");
    Collection collection{library};
    ShellControl shell{&collection};
    const QString payload = QString::fromLatin1(QByteArray("new bytes").toBase64());

    // A root path replaced with a symlink after selection must not become an
    // unapproved library. The real collection normally validates it on open.
    const QString redirectedRoot = top.filePath("redirected-root");
    require(QFile::link(outside, redirectedRoot), "root symlink fixture");
    collection.path = redirectedRoot;
    auto result = shell.importAsset("root.png", payload, "image/png");
    require(!result.value("ok").toBool(), "reject redirected root");
    require(!QFileInfo::exists(QDir(outside).filePath("Assets/images/root.png")), "no write through root link");
    collection.path = library;

    // Neither the broad Assets directory nor a typed bucket may lead out.
    require(QFile::link(outside, QDir(library).filePath("Assets")), "Assets symlink fixture");
    result = shell.importAsset("one.png", payload, "image/png");
    require(!result.value("ok").toBool(), "reject Assets symlink");
    require(!QFileInfo::exists(QDir(outside).filePath("images/one.png")), "no write through Assets symlink");
    require(QFile::remove(QDir(library).filePath("Assets")), "remove fixture link");

    require(QDir(library).mkpath("Assets"), "Assets fixture");
    require(QFile::link(outside, QDir(library).filePath("Assets/images")), "bucket symlink fixture");
    result = shell.importAsset("two.png", payload, "image/png");
    require(!result.value("ok").toBool(), "reject bucket symlink");
    require(!QFileInfo::exists(QDir(outside).filePath("two.png")), "no write through bucket symlink");
    require(QFile::remove(QDir(library).filePath("Assets/images")), "remove bucket link");

    result = shell.importAsset("three.png", payload, "image/png");
    require(result.value("ok").toBool(), "ordinary import succeeds");
    require(result.value("relative").toString() == "Assets/images/three.png", "portable relative path");
    require(readFile(QDir(library).filePath("Assets/images/three.png")) == "new bytes", "payload inside library");
    result = shell.importAsset("three.png", payload, "image/png");
    require(result.value("ok").toBool(), "collision import succeeds");
    require(result.value("relative").toString() == "Assets/images/three-2.png", "collision uses distinct name");

    const QString victim = QDir(outside).filePath("victim.png");
    require(writeFile(victim, "untouched"), "outside victim fixture");
    require(QFile::link(victim, QDir(library).filePath("Assets/images/four.png")), "leaf link fixture");
    result = shell.importAsset("four.png", payload, "image/png");
    require(readFile(victim) == "untouched", "never overwrite symlink target");
    require(result.value("ok").toBool() && result.value("relative").toString() == "Assets/images/four-2.png",
            "leaf symlink counts as a collision");

    const QString missing = QDir(outside).filePath("missing.png");
    require(QFile::link(missing, QDir(library).filePath("Assets/images/five.png")), "dangling leaf fixture");
    result = shell.importAsset("five.png", payload, "image/png");
    require(result.value("ok").toBool() && result.value("relative").toString() == "Assets/images/five-2.png",
            "dangling link counts as a collision");
    require(!QFileInfo::exists(missing), "dangling link cannot create outside target");

    const QString failed = QDir(library).filePath("Assets/images/failure.png");
    injectedPath = QFile::encodeName(failed);
    injectedWrites = 0;
    injectWriteFailure = true;
    result = shell.importAsset("failure.png", payload, "image/png");
    injectWriteFailure = false;
    require(!result.value("ok").toBool(), "short write followed by error must fail");
    require(injectedWrites == 2, "actually injected a short write followed by EIO");
    require(!QFileInfo::exists(failed), "failed import removes its partial asset");

    const QString replaced = QDir(library).filePath("Assets/images/replaced.png");
    const QString displaced = QDir(library).filePath("Assets/images/displaced-partial.png");
    injectedPath = QFile::encodeName(replaced);
    movedPartialPath = QFile::encodeName(displaced);
    injectedWrites = 0;
    injectWriteFailure = true;
    result = shell.importAsset("replaced.png", payload, "image/png");
    injectWriteFailure = false;
    require(!result.value("ok").toBool() && injectedWrites == 2, "replaced import fails at injected EIO");
    require(readFile(replaced) == "replacement", "cleanup must not remove a replaced inode");
    require(readFile(displaced) == "new", "injected rename preserved partial inode for inspection");
    std::puts("asset import security: PASS");
}
'''
    with tempfile.TemporaryDirectory(prefix="fanfold-import-") as directory:
        cpp = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        cpp.write_text(harness, encoding="utf-8")
        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Core"], text=True))
        subprocess.run(["c++", "-std=c++20", str(cpp), "-o", str(binary), "-Wl,--wrap=write", *flags], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
