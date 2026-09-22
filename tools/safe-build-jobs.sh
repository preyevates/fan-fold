#!/bin/sh
# Print a safe compiler job count for this machine's CURRENT free memory.
#
# A build launched with one job per core runs that many C++ compiler processes at once.
# Qt and QtWebEngine translation units routinely need 1.5-2 GB each, so a full-width
# build on a many-core machine can want far more RAM than is actually free once a
# desktop session is resident — and the kernel's OOM killer ends the build, usually at
# the worst moment.
#
# Size parallelism by AVAILABLE MEMORY rather than by core count. "Available" is the
# right figure, not "free": it counts reclaimable page cache the kernel will hand back
# under pressure.
#
# Budget: 2 GB per job, 4 GB reserved for the rest of the system, clamped to the core
# count, never fewer than one. Deliberately conservative — a slower build costs minutes,
# a killed build costs the whole run.
#
# Usage:  cmake --build build -j "$(tools/safe-build-jobs.sh)"
#         export DEB_BUILD_OPTIONS="parallel=$(tools/safe-build-jobs.sh)"
# Override with SAFE_BUILD_JOBS=<n>.
set -eu

if [ -n "${SAFE_BUILD_JOBS:-}" ]; then
    printf '%s\n' "$SAFE_BUILD_JOBS"
    exit 0
fi

gb_per_job="${SAFE_BUILD_GB_PER_JOB:-2}"
reserve_gb="${SAFE_BUILD_RESERVE_GB:-4}"

cores="$(nproc 2>/dev/null || echo 1)"

# MemAvailable is in kB; fall back to a single job if it cannot be read.
available_kb="$(awk '/^MemAvailable:/ {print $2; exit}' /proc/meminfo 2>/dev/null || true)"
if [ -z "$available_kb" ]; then
    echo 1
    exit 0
fi

usable_gb=$(( available_kb / 1024 / 1024 - reserve_gb ))
if [ "$usable_gb" -lt 1 ]; then
    echo 1
    exit 0
fi

jobs=$(( usable_gb / gb_per_job ))
[ "$jobs" -lt 1 ] && jobs=1
[ "$jobs" -gt "$cores" ] && jobs="$cores"

printf '%s\n' "$jobs"
