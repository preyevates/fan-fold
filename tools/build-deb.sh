#!/bin/bash
#
# Build the Fan Fold Debian package from a source tarball.
#
# Usage: tools/build-deb.sh <source-tarball> [output-dir]
#
#   source-tarball    produced by tools/make-source.sh
#   output-dir        where the .deb and its receipts are written (default: dist/)
#
# The package version is <upstream>-<debian-revision>, with the upstream version read
# from CMakeLists.txt. Set FANFOLD_DEBIAN_REVISION to repackage identical sources.
#
# The unreleased local theme catalogue is never packaged: debian/rules pins
# FANFOLD_LOCAL_THEME_CATALOG_DIR empty, which is the public default.

set -euo pipefail

usage() {
    echo "usage: $0 <source-tarball> [output-dir]" >&2
    exit 2
}

[ $# -ge 1 ] || usage

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tarball=$(readlink -f "$1")
outdir=$(readlink -f "${2:-$repo_root/dist}")

[ -f "$tarball" ] || { echo "no such tarball: $tarball" >&2; exit 1; }

upstream_version=$(sed -n 's/^project(.*VERSION \([0-9.]*\).*/\1/p' \
                   "$repo_root/CMakeLists.txt" | head -1)
[ -n "$upstream_version" ] || { echo "cannot read upstream version" >&2; exit 1; }

# Debian revision. Bump for a repackage of the same upstream source; the upstream
# version itself is owned by CMakeLists.txt.
debian_revision="${FANFOLD_DEBIAN_REVISION:-1}"
version="${upstream_version}-${debian_revision}"

workdir=$(mktemp -d "${TMPDIR:-/tmp}/fanfold-deb-XXXXXX")
trap 'rm -rf "$workdir"' EXIT

echo "==> unpacking $tarball"
tar -xzf "$tarball" -C "$workdir"
srcdir=$(find "$workdir" -mindepth 1 -maxdepth 1 -type d)
[ -d "$srcdir" ] || { echo "tarball did not contain a single top directory" >&2; exit 1; }

echo "==> installing debian/ packaging"
cp -a "$repo_root/packaging/debian" "$srcdir/debian"
chmod 0755 "$srcdir/debian/rules"

# The changelog is generated rather than tracked: its version is mechanical and it
# carries no content a human needs to maintain by hand.
cat > "$srcdir/debian/changelog" <<EOF
fanfold ($version) $(lsb_release -cs); urgency=medium

  * Fan Fold $upstream_version.

 -- preyevates <preyevates@users.noreply.github.com>  $(date -uR)
EOF

# Build parallelism is sized by AVAILABLE MEMORY, never by core count: Qt and
# QtWebEngine translation units need 1.5-2 GB each, so sizing by nproc on a many-core
# machine invites the OOM killer. safe-build-jobs.sh reads MemAvailable at call time and
# is re-read on every build, because the machine's free memory moves between runs.
jobs=$("$repo_root/tools/safe-build-jobs.sh")
echo "==> building $version with parallel=$jobs (memory-sized, $(nproc) cores present)"
( cd "$srcdir" && DEB_BUILD_OPTIONS="parallel=$jobs" dpkg-buildpackage -b -us -uc )

mkdir -p "$outdir"
deb=$(find "$workdir" -maxdepth 1 -name 'fanfold_*.deb' | head -1)
[ -f "$deb" ] || { echo "no .deb produced" >&2; exit 1; }
cp -a "$deb" "$outdir/"
built="$outdir/$(basename "$deb")"

echo "==> lintian"
lintian --tag-display-limit 0 "$built" 2>&1 | tee "$outdir/$(basename "$deb" .deb).lintian" || true

{
    echo "package:  $(dpkg-deb -f "$built" Package)"
    echo "version:  $(dpkg-deb -f "$built" Version)"
    echo "arch:     $(dpkg-deb -f "$built" Architecture)"
    echo "source:   $(basename "$tarball")"
    echo "srcsha:   $(sha256sum "$tarball" | cut -d' ' -f1)"
    echo "debsha:   $(sha256sum "$built" | cut -d' ' -f1)"
    echo "depends:  $(dpkg-deb -f "$built" Depends)"
} > "$outdir/$(basename "$deb" .deb).receipt"

echo
cat "$outdir/$(basename "$deb" .deb).receipt"
echo
echo "built: $built"
