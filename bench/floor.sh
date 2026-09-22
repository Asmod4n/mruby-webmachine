#!/bin/bash
# The floor: the ring with the smallest answer there is, so its number
# is the ceiling every later layer is measured against.
#
# The rules are the archive's, and each one is here because a number was
# once read wrong without it:
#
#   CONNS= is mandatory. The harness is part of the result, and a silent
#   default is how two runs of "the same bench" stop being comparable.
#
#   AF_UNIX by default. TCP loopback is a different machine's answer;
#   TRANSPORT=tcp asks for it on purpose.
#
#   One generator, htgen. Nothing else is installed and nothing else is
#   allowed: a script that names a tool nobody can run lies about how
#   its numbers were made.
#
#   A client-bound run is REFUSED, not recorded. If the client sits at
#   its limit while the server has room left, the number describes the
#   client. That is a conjunction: pegged client AND server headroom.
#
#   bad != 0 is refused. A run with refused connections or non-2xx
#   answers measured a server in trouble, not its floor.
#
#   REPS=n runs it n times and reports the median. One run is not a
#   number on this machine: the archive measured this very VM at
#   -c400 h1 unix, n=15, and got sd 18.2% with a 47% range. Nothing
#   below ~15% is resolvable here.
#
#   And the guard below cannot save a run where BOTH ends are pegged -
#   it refuses a pegged client over a server with headroom, and when
#   neither has any it is the scheduler that decides the number. That
#   case is named in the output rather than refused, because refusing
#   it would refuse every run this box can do.
#
#   CONNS=100 bench/floor.sh
#   CONNS=100 REPS=15 bench/floor.sh
#   CONNS=100 TRANSPORT=tcp PORT=8123 bench/floor.sh
#   CONNS=100 DURATION=20 bench/floor.sh
set -u

here=$(cd "$(dirname "$0")/.." && pwd)

[ -n "${CONNS:-}" ] || {
  echo "CONNS= is mandatory - the harness is part of the number" >&2
  exit 2
}
DURATION="${DURATION:-10}"
REPS="${REPS:-1}"
TRANSPORT="${TRANSPORT:-unix}"
PORT="${PORT:-8123}"
PATH_ASKED="${REQPATH:-/}"
SERVER="${SERVER:-$here/mruby/build/release/bin/webmachine-serve}"
ARM="${ARM:-}"

HTGEN="${HTGEN:-$(command -v htgen 2>/dev/null)}"
[ -x "${HTGEN:-}" ] || {
  echo "no htgen. Build it and point HTGEN= at the binary:" >&2
  echo "  git clone --recursive https://github.com/Asmod4n/htgen && make -C htgen" >&2
  exit 2
}
[ -x "$SERVER" ] || { echo "no server at $SERVER" >&2; exit 2; }

# htgen registers its buffer ring as locked memory and the server's ring
# is charged to the same user, so the shell's soft limit goes up and the
# line says what stood.
ulimit -l "$(ulimit -H -l)" 2>/dev/null || true
MEMLOCK_LINE="memlock=$(ulimit -l)"

HZ=$(getconf CLK_TCK)
WORK=$(mktemp -d)
SOCK="$WORK/wm.sock"
trap 'kill "${SRV:-0}" 2>/dev/null; rm -rf "$WORK"' EXIT

ticks_of() {
  awk '{ print $14, $15 }' "/proc/$1/stat" 2>/dev/null || echo "0 0"
}
# A child's ticks leave /proc the moment it is reaped, and times inside
# a command substitution counts the subshell's children rather than this
# shell's. Both of those read zero, and a zero here disables the
# client-bound rule silently - which is how this harness first reported
# client: 0% while refusing nothing. So the client is watched while it
# runs and the last reading that existed is the one used.
watch_the_client() {
  local last="0 0"
  while kill -0 "$1" 2>/dev/null; do
    local now
    now=$(awk '{ print $14, $15 }' "/proc/$1/stat" 2>/dev/null) && last="$now"
    sleep 0.2
  done
  echo "$last" > "$2"
}
machine_busy() {
  awk 'NR==1 { print $2+$3+$4+$7+$8+$9 }' /proc/stat
}

if [ "$TRANSPORT" = unix ]; then
  "$SERVER" "$SOCK" $ARM >"$WORK/srv.out" 2>&1 &
  SRV=$!
  WHERE=(--sock "$SOCK")
else
  "$SERVER" "$PORT" $ARM >"$WORK/srv.out" 2>&1 &
  SRV=$!
  WHERE=(--host 127.0.0.1 --port "$PORT")
fi

for _ in $(seq 1 100); do
  if [ "$TRANSPORT" = unix ]; then [ -S "$SOCK" ] && break; else sleep 0.05; break; fi
  sleep 0.05
done
kill -0 "$SRV" 2>/dev/null || { echo "the server did not come up:" >&2; cat "$WORK/srv.out" >&2; exit 1; }

CFLAGS_LINE=$(grep -o "'-[^']*'" "$here/build_config_release.rb" | tr -d "'" | sort -u |
  tr '\n' ' ' | sed 's/ $//')
echo "harness: floor htgen -c$CONNS -d${DURATION}s reps=$REPS transport=$TRANSPORT path=$PATH_ASKED arm=${ARM:-kernel} $MEMLOCK_LINE cflags=$CFLAGS_LINE $(uname -mr)"

RPS=()
BOTH_PEGGED=0
for rep in $(seq 1 "$REPS"); do
  M0=$(machine_busy)
  read -r SU0 SS0 <<<"$(ticks_of "$SRV")"
  "$HTGEN" "${WHERE[@]}" --conns "$CONNS" --seconds "$DURATION" --path "$PATH_ASKED" \
    >"$WORK/cli.out" 2>&1 &
  CLI=$!
  read -r CU0 CS0 <<<"$(ticks_of "$CLI")"
  watch_the_client "$CLI" "$WORK/cli.ticks" &
  WATCH=$!
  wait "$CLI" 2>/dev/null
  wait "$WATCH" 2>/dev/null
  read -r CU1 CS1 < "$WORK/cli.ticks"
  read -r SU1 SS1 <<<"$(ticks_of "$SRV")"
  M1=$(machine_busy)

  grep -q '^responses=' "$WORK/cli.out" || {
    echo "the client ended without a result:" >&2
    cat "$WORK/cli.out" >&2
    exit 1
  }
  if grep -q 'bad=[1-9]' "$WORK/cli.out"; then
    echo "REFUSED: bad != 0. That measured a server in trouble, not its floor." >&2
    exit 1
  fi

  SCPU=$(( ((SU1 - SU0) + (SS1 - SS0)) * 100 / HZ / DURATION ))
  CCPU=$(( ((CU1 - CU0) + (CS1 - CS0)) * 100 / HZ / DURATION ))
  OTHER=$(( (M1 - M0) * 100 / HZ / DURATION - SCPU - CCPU ))
  [ "$OTHER" -ge 0 ] || OTHER=0

  HEADROOM=15
  if [ "$CCPU" -ge 90 ] && [ "$SCPU" -le $((CCPU - HEADROOM)) ]; then
    echo "REFUSED: the client was pegged at ${CCPU}% while the server had ${SCPU}%, ${HEADROOM}+ points under it. This measures htgen, not webmachine. Drive the load from a second machine." >&2
    exit 1
  fi
  [ "$CCPU" -ge 90 ] && [ "$SCPU" -ge 90 ] && BOTH_PEGGED=1

  cat "$WORK/cli.out"
  echo "server: ${SCPU}% of one core   client: ${CCPU}% of one core   other: ${OTHER}% of one core"
  RPS+=("$(grep -o 'rps=[0-9]*' "$WORK/cli.out" | cut -d= -f2)")
done

MEDIAN=$(printf '%s\n' "${RPS[@]}" | sort -n | awk '{ a[NR] = $1 }
  END { print (NR % 2) ? a[(NR + 1) / 2] : int((a[NR / 2] + a[NR / 2 + 1]) / 2) }')
SPREAD=$(printf '%s\n' "${RPS[@]}" | sort -n | awk -v m="$MEDIAN" '{ a[NR] = $1 }
  END { printf "%.1f", (m > 0 ? (a[NR] - a[1]) * 100 / m : 0) }')
echo "median rps: $MEDIAN over $REPS run(s), range ${SPREAD}% of the median"
if [ "$BOTH_PEGGED" = 1 ]; then
  echo "NOTE: both ends were pegged, so the scheduler decided the split. The archive measured this shape on a four cpu vm at n=15 and got sd 18.2% with a 47% range - nothing below ~15% is resolvable here, and the load has to come from a second machine to do better."
fi

mkdir -p "$here/bench/results"
{
  echo "harness: floor htgen -c$CONNS -d${DURATION}s reps=$REPS transport=$TRANSPORT path=$PATH_ASKED arm=${ARM:-kernel} $MEMLOCK_LINE cflags=$CFLAGS_LINE $(uname -mr)"
  printf 'rps:'
  printf ' %s' "${RPS[@]}"
  printf '\n'
  echo "median rps: $MEDIAN over $REPS run(s), range ${SPREAD}% of the median"
  [ "$BOTH_PEGGED" = 1 ] && echo "both ends pegged - the scheduler decided the split"
  echo
} >> "$here/bench/results/floor.log"
