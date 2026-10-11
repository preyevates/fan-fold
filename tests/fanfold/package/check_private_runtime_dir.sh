#!/bin/sh
# The package test launches Fan Fold under its own D-Bus session. That bus must not share the
# host's XDG_RUNTIME_DIR: a bus that is not systemd-activated starts xdg-document-portal by
# direct exec, and that portal always mounts its FUSE filesystem at $XDG_RUNTIME_DIR/doc,
# first unmounting whatever is already there. Under the host's runtime dir that takes the
# user's live portal down.
#
# Runs the real package test with a stand-in dbus-run-session that records the runtime dir it
# was given instead of starting a bus, so nothing here can touch the host's portal.
set -eu

build_dir=$1
source_dir=$2
local_theme_bundle=$3
here=$(dirname "$0")

scratch=$(mktemp -d "${TMPDIR:-/tmp}/fanfold-runtime-check.XXXXXX")
trap 'rm -rf "$scratch"' EXIT INT TERM
mkdir -p "$scratch/bin"
record="$scratch/record"

cat > "$scratch/bin/dbus-run-session" <<STUB
#!/bin/sh
printf '%s\n' "\${XDG_RUNTIME_DIR-}" > "$record"
if [ -n "\${XDG_RUNTIME_DIR-}" ] && [ -d "\$XDG_RUNTIME_DIR" ]; then
    stat -c %a "\$XDG_RUNTIME_DIR" >> "$record"
fi
exit 124
STUB
chmod +x "$scratch/bin/dbus-run-session"

host_runtime=${XDG_RUNTIME_DIR-}
[ -n "$host_runtime" ] || host_runtime=/run/user/$(id -u)

set +e
PATH="$scratch/bin:$PATH" XDG_RUNTIME_DIR="$host_runtime" \
    FANFOLD_PACKAGE_TEST_WORK_DIR="$scratch/work" \
    sh "$here/run_package_test.sh" "$build_dir" "$source_dir" "$local_theme_bundle" \
    >"$scratch/package.log" 2>&1
status=$?
set -e

if [ "$status" -eq 77 ]; then
    cat "$scratch/package.log" >&2
    exit 77
fi
if [ "$status" -ne 0 ]; then
    printf 'package test failed under the stand-in bus (%s)\n' "$status" >&2
    cat "$scratch/package.log" >&2
    exit 1
fi
if [ ! -s "$record" ]; then
    printf 'the package test never started a D-Bus session\n' >&2
    exit 1
fi

seen=$(sed -n 1p "$record")
mode=$(sed -n 2p "$record")
case "$seen" in
    '' | "$host_runtime" | "$host_runtime"/*)
        printf 'private bus runs in the host runtime dir (%s): it would unmount the live document portal\n' \
            "${seen:-<unset>}" >&2
        exit 1
        ;;
esac
if [ "$mode" != 700 ]; then
    printf 'private runtime dir %s has mode %s, expected 700\n' "$seen" "$mode" >&2
    exit 1
fi
printf 'private session bus uses its own runtime dir: PASS\n'
