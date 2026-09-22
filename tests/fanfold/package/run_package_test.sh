#!/bin/sh
# Exercise Fan Fold exactly as a staged Linux package, without touching a live install.
set -eu

build_dir=$1
source_dir=$2
local_theme_bundle=$3
work_dir="$build_dir/package-test"
stage="$work_dir/stage"
xdg="$work_dir/xdg"
notes="$work_dir/notes"
state="$work_dir/state"
log="$work_dir/launch.log"

rm -rf "$work_dir"
mkdir -p "$stage" "$xdg/config" "$xdg/cache" "$xdg/data" "$xdg/state" "$notes" "$state"
printf '# Package smoke test\n\nOffline editor asset check.\n' > "$notes/Welcome.md"

DESTDIR="$stage" cmake --install "$build_dir" --prefix /usr

prefix="$stage/usr"
for path in \
    bin/fanfold \
    share/applications/io.github.preyevates.FanFold.desktop \
    share/icons/hicolor/scalable/apps/io.github.preyevates.FanFold.svg \
    share/icons/hicolor/scalable/apps/io.github.preyevates.FanFold-symbolic.svg \
    share/fanfold/qml/Main.qml \
    share/fanfold/qml/PinnedNoteWindow.qml \
    share/fanfold/qml/QuietButton.qml \
    share/fanfold/qml/LayoutContract.js \
    share/fanfold/qml/editor.html \
    share/fanfold/qml/editor.js \
    share/fanfold/qml/vendor/package/dist/index.min.js \
    share/fanfold/qml/vendor/package/dist/index.css \
    share/doc/fanfold/LICENSE \
    share/doc/fanfold/THIRD_PARTY_NOTICES.md \
    share/doc/fanfold/licenses/VDITOR-MIT.txt \
    share/doc/fanfold/licenses/DIFF-MATCH-PATCH-APACHE-2.0.txt \
    share/doc/fanfold/licenses/HIGHLIGHTJS-BSD-3-CLAUSE.txt \
    share/doc/fanfold/licenses/HIGHLIGHTJS-SOLIDITY-WTFPL.txt \
    share/doc/fanfold/licenses/HIGHLIGHTJS-ABAP-MIT.txt \
    share/doc/fanfold/licenses/HIGHLIGHTJS-HLSL-MIT.txt \
    share/doc/fanfold/licenses/HIGHLIGHTJS-GDSCRIPT-BSD-3-CLAUSE.txt \
    share/doc/fanfold/licenses/ANT-DESIGN-ICONS-MIT.txt \
    share/doc/fanfold/licenses/LUTE-MULAN-PSL-2.0.txt; do
    test -e "$prefix/$path" || {
        printf 'missing packaged path: %s\n' "$path" >&2
        exit 1
    }
done
if [ "$local_theme_bundle" = ON ]; then
    for path in \
        share/fanfold/themes/catalog.json \
        share/fanfold/themes/manifest.json \
        share/fanfold/themes/NOTICES.md; do
        test -f "$prefix/$path" || {
            printf 'missing local-only theme path: %s\n' "$path" >&2
            exit 1
        }
    done
else
    test ! -e "$prefix/share/fanfold/themes"
fi
test ! -e "$prefix/share/autostart/io.github.preyevates.FanFold.desktop"
test ! -e "$prefix/share/fanfold/qml/.qmllint.ini"
for excluded in \
    share/fanfold/qml/vendor/package/dist/ts \
    share/fanfold/qml/vendor/package/dist/method.js \
    share/fanfold/qml/vendor/package/dist/js/mermaid \
    share/fanfold/qml/vendor/package/dist/js/mathjax; do
    test ! -e "$prefix/$excluded"
done

if find "$prefix/share/fanfold" -type l | grep -q .; then
    printf 'packaged runtime assets contain symbolic links\n' >&2
    exit 1
fi

grep -q '^Name=Fan Fold$' "$prefix/share/applications/io.github.preyevates.FanFold.desktop"
grep -q '^Exec=fanfold$' "$prefix/share/applications/io.github.preyevates.FanFold.desktop"
grep -q '^Icon=io.github.preyevates.FanFold$' "$prefix/share/applications/io.github.preyevates.FanFold.desktop"
color_icon="$source_dir/src/fanfold/packaging/io.github.preyevates.FanFold.svg"
symbolic_icon="$source_dir/src/fanfold/packaging/io.github.preyevates.FanFold-symbolic.svg"
test "$(sha256sum "$color_icon" | cut -d ' ' -f 1)" = \
    e6ddb4f6870f96cfc811bf4d6c0509de90c1f1f300bc37dfe6d6a7a4cc8a1be5
test "$(sha256sum "$symbolic_icon" | cut -d ' ' -f 1)" = \
    faba15254ac5a4dbb4b942b1b3a14ee61b7af2918c41519fcff6fbac4e04cf3f
cmp -s "$color_icon" \
    "$prefix/share/icons/hicolor/scalable/apps/io.github.preyevates.FanFold.svg"
cmp -s "$symbolic_icon" \
    "$prefix/share/icons/hicolor/scalable/apps/io.github.preyevates.FanFold-symbolic.svg"
if grep -R -F "$source_dir" "$prefix/share/fanfold" "$prefix/share/applications" >/dev/null; then
    printf 'packaged runtime contains a source-checkout path\n' >&2
    exit 1
fi

private_display=1
if ! TMPDIR="$work_dir" xvfb-run -e "$work_dir/xvfb-probe.log" -a xdpyinfo >/dev/null 2>&1; then
    private_display=0
    printf 'SKIP: installed runtime launch needs a private Xvfb display\n' >&2
    head -n 80 "$work_dir/xvfb-probe.log" >&2
else
    set +e
    (
        cd /
        XDG_CONFIG_HOME="$xdg/config" \
        XDG_CACHE_HOME="$xdg/cache" \
        XDG_DATA_HOME="$xdg/data" \
        XDG_STATE_HOME="$xdg/state" \
        timeout --signal=TERM 8s xvfb-run -a "$prefix/bin/fanfold" \
            --root "$notes" --state "$state" >"$log" 2>&1
    )
    status=$?
    set -e
    case "$status" in
        124|143) ;;
        *)
            printf 'installed Fan Fold exited unexpectedly (%s)\n' "$status" >&2
            cat "$log" >&2
            exit 1
            ;;
    esac
    if grep -E 'QQmlApplicationEngine failed|module ".*" is not installed|No such file or directory|ERR_FILE_NOT_FOUND' "$log" >/dev/null; then
        printf 'installed Fan Fold reported a runtime asset/import failure\n' >&2
        cat "$log" >&2
        exit 1
    fi
fi

# A reinstall models an in-place package upgrade. Application-owned and user-owned data
# must survive both reinstall and manifest-driven package removal.
printf 'keep-state\n' > "$state/package-preservation.txt"
mkdir -p "$xdg/data/fanfold/theme-imports/existing-private"
printf 'keep-private-theme\n' > \
    "$xdg/data/fanfold/theme-imports/existing-private/preservation.txt"
DESTDIR="$stage" cmake --install "$build_dir" --prefix /usr >/dev/null
test "$(cat "$state/package-preservation.txt")" = keep-state
test "$(cat "$xdg/data/fanfold/theme-imports/existing-private/preservation.txt")" = \
    keep-private-theme
test "$(cat "$notes/Welcome.md")" = '# Package smoke test

Offline editor asset check.'

test -s "$build_dir/install_manifest.txt"
DESTDIR="$stage" cmake --build "$build_dir" --target uninstall >/dev/null

test ! -e "$prefix/bin/fanfold"
test ! -e "$prefix/share/fanfold/themes"
test -f "$notes/Welcome.md"
test -f "$state/package-preservation.txt"
test "$(cat "$xdg/data/fanfold/theme-imports/existing-private/preservation.txt")" = \
    keep-private-theme
test -d "$xdg/config"
test -d "$xdg/cache"

printf 'staged install, assets, upgrade, manifest uninstall, and preservation: PASS\n'

rm -rf "$work_dir"
test ! -e "$work_dir"
if [ "$private_display" -eq 0 ]; then
    exit 77
fi
