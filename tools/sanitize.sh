#!/usr/bin/env bash
# Builds the core and its test programs with AddressSanitizer and UndefinedBehaviorSanitizer and runs every test.
# Linux or WSL, about 4 minutes. Any report fails the run.
#
#   tools/sanitize.sh [jobs]      (default 4 jobs; build output goes to build-sanitize/)
set -euo pipefail
# Note: piping the output into tail hides the exit status; check the last line ("all ok") or use PIPESTATUS.

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build-sanitize"
jobs="${1:-4}"
flags="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g"   # Release (-O2): the timing checks of the tests need optimized code

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_EXE_LINKER_FLAGS="$flags" >/dev/null
cmake --build "$build" -j"$jobs"

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

status=0
for test in corpus-core-test corpus-roundtrip-test corpus-psd-test corpus-audio-depth-test corpus-audio-tags-test \
            corpus-content-test corpus-update-test corpus-litehtml-test; do
    printf '%-28s' "$test"
    if "$build/$test" "$work" >"$work/$test.log" 2>&1; then
        echo ok
    else
        echo FAILED
        tail -40 "$work/$test.log"
        status=1
    fi
done
[ $status -eq 0 ] && echo "all ok"
exit $status
