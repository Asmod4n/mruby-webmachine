#!/bin/bash
set -u
cd "$(dirname "$0")/.."
BIN="${BIN:-bench/run}"
RUNS="${RUNS:-2000}"

usage()
{
    cat <<'TEXT'
usage: bench/instructions.sh [-h] <arm> [<arm> ...]

Counts instructions per iteration of a bench arm with callgrind. Each arm
is run at RUNS and at 2*RUNS iterations, and the count is the difference
divided by RUNS.

arguments:
  <arm>        name of a benchmark arm, matched whole

options:
  -h, --help   print this text and exit

environment:
  BIN          benchmark binary to run (default: bench/run)
  RUNS         iterations of the first run (default: 2000)

build the binary with:
  WM_MARCH=x86-64-v3 rake bench
TEXT
}

case "${1:-}" in
-h | --help)
    usage
    exit 0
    ;;
esac

command -v valgrind >/dev/null || {
    echo "valgrind is not installed" >&2
    exit 2
}
[ -x "$BIN" ] || {
    echo "$BIN is not there; run rake bench first" >&2
    exit 2
}
[ $# -ge 1 ] || {
    usage >&2
    exit 2
}

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
    grep -m1 -E '^(summary|totals):' "$out" | awk '{print $2}'
}

printf '%-34s %14s\n' arm 'instructions'
for arm in "$@"; do
    one=$(count_at "$arm" "$RUNS")
    two=$(count_at "$arm" $((RUNS * 2)))
    [ -n "$one" ] && [ -n "$two" ] || {
        echo "$arm: callgrind wrote no total" >&2
        exit 1
    }
    printf '%-34s %14.1f\n' "$arm" "$(echo "($two - $one) / $RUNS" | bc -l)"
done
