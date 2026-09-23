#!/usr/bin/env bash
# One comparison, built and run in every permutation RULES.md names:
# g++ and clang, -Os -O2 -O3, x86-64-v3 and x86-64-v4. One kind of test
# per binary, so every test is a binary of its own in each permutation.
#
#   bench/matrix.sh <arms.cpp> <MACRO> <test> [<test> ...]
#
#   bench/matrix.sh bench/find_arms.cpp FIND_TEST 1 2
#   bench/matrix.sh bench/run_arms.cpp RUN_TEST 1 2 3
#
# The binaries run one after the other, never side by side, at nice -15,
# never pinned to a core, ten repetitions of at least half a second, the
# arms interleaved at random.
# Every row keeps its coefficient of variation beside the median. A build that
# fails is a cell with its error, not a gap: the error is written beside
# the results and named at the end.
set -u

SOURCE="$1"
MACRO="$2"
shift 2
TESTS=("$@")
[ "${#TESTS[@]}" -gt 0 ] || { echo "name at least one test" >&2; exit 2; }

HERE="$(cd "$(dirname "$0")/.." && pwd)"
NAME="$(basename "$SOURCE" .cpp)"
STAMP="$(date -u +%Y-%m-%d-%H%M%SZ)"
OUT="$HERE/bench/results/$STAMP-$NAME-matrix"
BUILT="$(mktemp -d)"
mkdir -p "$OUT"

ADA="$(ls -d "$HERE"/mruby/build/repos/*/mruby-uri-parser 2>/dev/null | head -1)"
INCLUDES=(-I"$HERE/src" -I"$HERE/bench")
[ -n "$ADA" ] && INCLUDES+=(-I"$ADA/include")

REPETITIONS="${BENCH_REPETITIONS:-10}"
# Without the right to raise priority nice says so on stderr and runs anyway.
RUN=(nice -n -15)

failed=()
for CXX in g++-16 clang++-23; do
  ALIGN="-falign-functions=64 -falign-loops=64"
  [ "$CXX" = g++-16 ] && ALIGN="$ALIGN -falign-jumps=64"
  for O in Os O2 O3; do
    for MARCH in x86-64-v3 x86-64-v4; do
      for TEST in "${TESTS[@]}"; do
        cell="$CXX-$O-$MARCH-test$TEST"
        if ! $CXX -std=c++26 -"$O" -march="$MARCH" $ALIGN -D"$MACRO=$TEST" "${INCLUDES[@]}" \
            "$HERE/$SOURCE" -lbenchmark -lbenchmark_main -lpthread -o "$BUILT/$cell" 2>"$OUT/$cell.build.txt"; then
          failed+=("$cell")
          continue
        fi
        [ -s "$OUT/$cell.build.txt" ] || rm -f "$OUT/$cell.build.txt"
        "${RUN[@]}" "$BUILT/$cell" --benchmark_repetitions="$REPETITIONS" --benchmark_min_time=0.5s \
          --benchmark_enable_random_interleaving=true --benchmark_report_aggregates_only=true \
          --benchmark_context=ran_as="$(id -un)" \
          --benchmark_out="$OUT/$cell.json" >/dev/null ||
          failed+=("$cell ran")
      done
    done
  done
done
rm -rf "$BUILT"

echo "$OUT"
for cell in "${failed[@]}"; do echo "failed: $cell"; done
