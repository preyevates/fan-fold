"""Compile and exercise the actual secure icon-export helper on disposable roots."""
import shlex
import subprocess
import tempfile
from pathlib import Path

HEADER = Path(__file__).resolve().parents[2] / "src/shell/iconexport.h"


def main():
    code = r'''
#include "iconexport.h"
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
static void require(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
static QByteArray read(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
int main() {
    QTemporaryDir sandbox;
    require(sandbox.isValid(), "sandbox");
    QDir d(sandbox.path());
    require(d.mkpath("library") && d.mkpath("outside"), "fixtures");
    const QString root = d.filePath("library"), outside = d.filePath("outside");
    const QString redirected = d.filePath("redirected");
    require(QFile::link(outside, redirected), "redirected root");
    require(FanFold::exportIconBytes(redirected, "note", "abc", "png").isEmpty(), "root symlink rejected");
    require(QFile::link(outside, QDir(root).filePath("Assets")), "Assets link");
    require(FanFold::exportIconBytes(root, "note", "abc", "png").isEmpty(), "Assets symlink rejected");
    require(QFile::remove(QDir(root).filePath("Assets")), "unlink Assets");
    require(QDir(root).mkpath("Assets"), "real Assets");
    require(QFile::link(outside, QDir(root).filePath("Assets/icons")), "icons link");
    require(FanFold::exportIconBytes(root, "note", "abc", "png").isEmpty(), "icons symlink rejected");
    require(QFile::remove(QDir(root).filePath("Assets/icons")), "unlink icons");
    require(FanFold::exportIconBytes(root, "../evil", "abc", "png").isEmpty(), "traversal rejected");
    const QString relative = FanFold::exportIconBytes(root, "note", "abc", "png");
    require(relative == "Assets/icons/fanfold-icon-note.png", "real export path");
    require(read(QDir(root).filePath(relative)) == "abc", "real export bytes");
    require(FanFold::safeExistingIcon(root, relative), "safe existing icon");
    require(!FanFold::safeExistingIcon(root, "Assets/icons/../evil.png"), "existing traversal rejected");
    require(FanFold::exportIconBytes(root, "note", "abc", "png") == relative, "reuse identical");
    require(FanFold::exportIconBytes(root, "note", "def", "png") == "Assets/icons/fanfold-icon-note-1.png", "collision");
    QFile victim(QDir(outside).filePath("victim.png"));
    require(victim.open(QIODevice::WriteOnly) && victim.write("untouched") == 9, "outside victim");
    victim.close();
    require(QFile::link(victim.fileName(), QDir(root).filePath("Assets/icons/fanfold-icon-link.png")), "leaf symlink");
    require(!FanFold::safeExistingIcon(root, "Assets/icons/fanfold-icon-link.png"), "existing symlink rejected");
    require(FanFold::exportIconBytes(root, "link", "abc", "png") == "Assets/icons/fanfold-icon-link-1.png", "link collision");
    require(read(victim.fileName()) == "untouched", "outside unchanged");
    std::puts("icon export security: PASS");
}
'''
    with tempfile.TemporaryDirectory(prefix="fanfold-icon-") as directory:
        source = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        source.write_text(code)
        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Core"], text=True))
        subprocess.run(["c++", "-std=c++20", "-I", str(HEADER.parent), str(source), "-o", str(binary), *flags], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
