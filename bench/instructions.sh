#!/bin/bash
# Instructions per iteration of one bench arm, counted by callgrind.
#
#   bench/instructions.sh <arm> [<arm> ...]
#
# A clock on a shared host swings; an instruction count of the same
# binary does not move, so a change of one percent shows. That is what
# this exists for, and it is the only tool for a change the clock cannot
# resolve.
#
# The count comes from the difference of two runs, N and 2N iterations of
# the same arm. Start-up, the timing library's own work and everything
# else outside the loop stand in both runs and cancel exactly, so no
# empty arm has to be trusted.
#
# The binary has to be built without AVX-512, because valgrind does not
# decode it and the run dies on the first unknown instruction:
#
#   WM_MARCH=x86-64-v3 rake bench
#
# Two limits, and together they keep this tool away from SIMD.
#
# Callgrind counts instructions, not micro-operations. SSE4.2 pcmpestri
# is one instruction and about eight micro-operations, so an arm that
# swaps it for several cheaper instructions reads worse here than it
# runs.
#
# And valgrind 3.22 does not decode pcmpestri with a memory operand.
# Measured here with four line programs: `pcmpestri xmm, xmm` runs and
# `pcmpestri xmm, m128` raises SIGILL, in the legacy encoding under
# -march=x86-64-v2 and in the VEX encoding under -march=x86-64-v3 alike.
# An optimising compiler folds picohttpparser's load into the
# instruction, so an optimised build of it cannot be counted at all.
#
# So: this counts scalar work. Where an arm changes which vector
# instructions are used, the clock is the tool and this is not.
#
set -u
cd "$(dirname "$0")/.."
BIN="${BIN:-bench/run}"
RUNS="${RUNS:-2000}"
command -v valgrind >/dev/null || { echo "valgrind is not installed" >&2; exit 2; }
[ -x "$BIN" ] || { echo "$BIN is not there; run rake bench first" >&2; exit 2; }
[ $# -ge 1 ] || { echo "usage: bench/instructions.sh <arm> [<arm> ...]" >&2; exit 2; }

WORK=$(mktemp -d /tmp/wm-ir.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

count_at()
{
    local arm="$1"
    local iterations="$2"
    local out="$WORK/$arm.$iterations"
    valgrind --tool=callgrind --callgrind-out-file="$out" \
        "$BIN" "--benchmark_filter=^$arm\$" "--benchmark_min_time=${iterations}x" \
        >"$out.log" 2>"$out.err" || {
        echo "the run failed; its output is in $out.err" >&2
        cat "$out.err" >&2
        exit 1
    }
    # callgrind writes 'summary:' or, without a summary line, 'totals:'.
    grep -m1 -E '^(summary|totals):' "$out" | awk '{print $2}'
}

printf '%-34s %14s\n' arm 'instructions'
for arm in "$@"; do
    one=$(count_at "$arm" "$RUNS")
    two=$(count_at "$arm" $((RUNS * 2)))
    [ -n "$one" ] && [ -n "$two" ] || { echo "$arm: callgrind wrote no total" >&2; exit 1; }
    printf '%-34s %14.1f\n' "$arm" "$(echo "($two - $one) / $RUNS" | bc -l)"
done
