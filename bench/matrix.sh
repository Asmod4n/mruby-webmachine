#!/usr/bin/env bash
# One comparison, built and run in every permutation RULES.md names:
# g++ and clang, -Os and -O2, at x86-64-v4. One kind of test
# per binary, so every test is a binary of its own in each permutation.
#
#   bench/matrix.sh <arms.cpp> <MACRO> <test> [<test> ...]
#
#   bench/matrix.sh bench/find_arms.cpp FIND_TEST 1 2
#   bench/matrix.sh bench/run_arms.cpp RUN_TEST 0 16 32 48 64 96 128 256
#
# Every binary is built first. Then the runs go in rounds: each round
# runs every binary once, in an order drawn at random once before the
# first round, one repetition with its arms interleaved at random. A
# drift of the machine so falls on every cell alike instead of on one
# column.
# Nothing is pinned to a core; the runs go at nice -15, one after the
# other. Google Benchmark writes the load average into every round, and
# bench/matrix_table.rb reads the rounds back into one table.
#
# A build that fails is a cell with its error, not a gap: the error is
# written beside the results and named at the end.
set -u

SOURCE="$1"
MACRO="$2"
shift 2
TESTS=("$@")
[ "${#TESTS[@]}" -gt 0 ] || { echo "name at least one test" >&2; exit 2; }

HERE="$(cd "$(dirname "$0")/.." && pwd)"
NAME="$(basename "$SOURCE" .cpp)"
STAMP="$(date -u +%Y-%m-%d-%H%M%SZ)"
OUT="${BENCH_RESULTS:-$HERE/bench/results}/$STAMP-$NAME-matrix"
BUILT="$(mktemp -d)"
mkdir -p "$OUT"

ADA="${BENCH_ADA:-$(ls -d "$HERE"/mruby/build/repos/*/mruby-uri-parser 2>/dev/null | head -1)}"
INCLUDES=(-I"$HERE/src" -I"$HERE/bench")
[ -n "$ADA" ] && INCLUDES+=(-I"$ADA/include")

ROUNDS="${BENCH_ROUNDS:-10}"
# The levels and the instruction set; RULES.md names the defaults.
LEVELS="${BENCH_LEVELS:-Os O2}"
MARCH_SET="${BENCH_MARCH:-x86-64-v4}"
# More -D flags for every build, the same in every cell.
EXTRA="${BENCH_CXXFLAGS:-}"
MIN_TIME="${BENCH_MIN_TIME:-0.5s}"
# Without the right to raise priority nice says so on stderr and runs anyway.
RUN=(nice -n -15)

# The binaries are built by make, eight at a time, because rake builds
# one after the other.
failed=()
cells=()
{
  printf 'all:'
  for CXX in g++-16 clang++-23; do for O in $LEVELS; do for MARCH in $MARCH_SET; do for TEST in "${TESTS[@]}"; do
    printf ' %s' "$CXX-$O-$MARCH-test$TEST"
  done; done; done; done
  printf '\n'
  for CXX in g++-16 clang++-23; do
    ALIGN="-falign-functions=64 -falign-loops=64"
    [ "$CXX" = g++-16 ] && ALIGN="$ALIGN -falign-jumps=64"
    for O in $LEVELS; do
      for MARCH in $MARCH_SET; do
        for TEST in "${TESTS[@]}"; do
          cell="$CXX-$O-$MARCH-test$TEST"
          printf '%s:\n\t%s -std=c++26 -%s -march=%s %s -D%s=%s %s %s -lbenchmark -lbenchmark_main -lpthread -o %s 2>%s\n' \
            "$cell" "$CXX" "$O" "$MARCH" "$ALIGN $EXTRA" "$MACRO" "$TEST" "${INCLUDES[*]}" "$HERE/$SOURCE" \
            "$BUILT/$cell" "$OUT/$cell.build.txt"
        done
      done
    done
  done
} > "$BUILT/Makefile"
make -k -j8 -C "$BUILT" all >/dev/null
for cell in $(sed -n '1s/^all://p' "$BUILT/Makefile"); do
  if [ -x "$BUILT/$cell" ]; then
    [ -s "$OUT/$cell.build.txt" ] || rm -f "$OUT/$cell.build.txt"
    mkdir -p "$OUT/$cell"
    cells+=("$cell")
  else
    failed+=("$cell")
  fi
done
echo "built ${#cells[@]} binaries, ${#failed[@]} failed; $ROUNDS rounds follow"

# The order is drawn once, before the first round, and every round keeps it.
order=$(printf '%s\n' "${cells[@]}" | shuf)
for round in $(seq 1 "$ROUNDS"); do
  for cell in $order; do
    "${RUN[@]}" "$BUILT/$cell" --benchmark_repetitions=1 --benchmark_min_time="$MIN_TIME" \
      --benchmark_enable_random_interleaving=true \
      --benchmark_context=ran_as="$(id -un)" --benchmark_context=round="$round" \
      --benchmark_out="$OUT/$cell/round-$round.json" >/dev/null ||
      failed+=("$cell ran in round $round")
  done
  echo "round $round of $ROUNDS done"
done
rm -rf "$BUILT"

echo "$OUT"
for cell in "${failed[@]}"; do echo "failed: $cell"; done
ruby "$HERE/bench/matrix_table.rb" "$OUT"
