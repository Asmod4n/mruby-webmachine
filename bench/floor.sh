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
#   SYSCALLS=1 counts the server's syscalls with perf and divides the
#   responses by them. It is opt-in because counting needs perf, a
#   mounted tracefs and a paranoid setting most machines do not have
#   lying around, and a probe that warns on every run is noise for
#   anyone not asking the question. Off, the line is not printed.
#
#   LATENCY=1 asks htgen for the distribution beside the rate. Read it
#   at the concurrency you mean, never at the one that gives the best
#   rate: at the rate's plateau the number is the queue.
#
#   CONNS=100 bench/floor.sh
#   CONNS=100 REPS=15 bench/floor.sh
#   NICE=-15 runs the server at that nice level, which is what the
#   archive's rows carry. It changes who the scheduler prefers on a box
#   where both ends are pegged, so it belongs in the harness line.
#
#   CONNS=100 SYSCALLS=1 LATENCY=1 bench/floor.sh
#   CONNS=1536 DURATION=10 NICE=-15 bench/floor.sh
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
NICE_ASK=()
[ -n "${NICE:-}" ] && NICE_ASK=(nice -n "$NICE")
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

# Opt-in, and it says why it cannot when it cannot: tracepoints are
# gated separately from cpu events, so perf record can work while this
# counter stays empty. A column of silent '-' hides that.
SYSC_PERF=""
if [ "${SYSCALLS:-0}" = 1 ]; then
  SYSC_PERF="${PERF:-}"
  if [ -z "$SYSC_PERF" ]; then
    if perf stat -e raw_syscalls:sys_enter -x, -- /bin/true 2>&1 >/dev/null | grep -q '^[0-9]'
    then SYSC_PERF=perf
    else SYSC_PERF=$(ls /usr/lib/linux-tools-*/perf 2>/dev/null | head -1); fi
  fi
  SYSC_PROBE=$("${SYSC_PERF:-perf}" stat -e raw_syscalls:sys_enter -x, -- /bin/true 2>&1 >/dev/null)
  if ! echo "$SYSC_PROBE" | grep -q '^[0-9]'; then
    echo "req/syscall: unavailable - ${SYSC_PERF:-perf} cannot count raw_syscalls:sys_enter. Its own words:" >&2
    echo "$SYSC_PROBE" | head -3 | sed 's/^/    /' >&2
    echo "  Usual causes: kernel.perf_event_paranoid > -1 without CAP_PERFMON, or /sys/kernel/tracing not mounted." >&2
    SYSC_PERF=""
  fi
fi
SYSC_PID=
SYSC_OUT="$WORK/sysc"
# The wait must run in the shell that backgrounded perf: a $( ) subshell
# is not its parent, so its wait returns at once while the file is still
# being written. Same split as the client watcher above.
sysc_begin() {
  [ -n "$SYSC_PERF" ] || return 0
  "$SYSC_PERF" stat -e raw_syscalls:sys_enter -x, -p "$1" -o "$SYSC_OUT" \
    -- sleep "$2" >/dev/null 2>&1 &
  SYSC_PID=$!
}
sysc_read() {
  awk -F, '$3 == "raw_syscalls:sys_enter" && $1 ~ /^[0-9]/ { print $1 }' "$SYSC_OUT" 2>/dev/null
}

LATENCY_ASK=()
[ "${LATENCY:-0}" = 1 ] && LATENCY_ASK=(--latency)


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
  "${NICE_ASK[@]+"${NICE_ASK[@]}"}" "$SERVER" "$SOCK" $ARM >"$WORK/srv.out" 2>&1 &
  SRV=$!
  WHERE=(--sock "$SOCK")
else
  "${NICE_ASK[@]+"${NICE_ASK[@]}"}" "$SERVER" "$PORT" $ARM >"$WORK/srv.out" 2>&1 &
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
echo "harness: floor htgen -c$CONNS -d${DURATION}s reps=$REPS transport=$TRANSPORT path=$PATH_ASKED arm=${ARM:-kernel} nice=${NICE:-default} $MEMLOCK_LINE cflags=$CFLAGS_LINE $(uname -mr)"

RPS=()
BOTH_PEGGED=0
for rep in $(seq 1 "$REPS"); do
  M0=$(machine_busy)
  read -r SU0 SS0 <<<"$(ticks_of "$SRV")"
  sysc_begin "$SRV" "$DURATION"
  "$HTGEN" "${WHERE[@]}" --conns "$CONNS" --seconds "$DURATION" --path "$PATH_ASKED" \
    "${LATENCY_ASK[@]+"${LATENCY_ASK[@]}"}" >"$WORK/cli.out" 2>&1 &
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
  if [ -n "$SYSC_PERF" ]; then
    [ -n "$SYSC_PID" ] && wait "$SYSC_PID" 2>/dev/null
    SYSC_PID=
    NSYSC=$(sysc_read)
    NDONE=$(grep -o 'responses=[0-9]*' "$WORK/cli.out" | cut -d= -f2)
    if [ -n "$NSYSC" ] && [ "$NSYSC" -gt 0 ] && [ -n "$NDONE" ]; then
      awk -v d="$NDONE" -v n="$NSYSC" 'BEGIN { printf "req/syscall: %.1f (%d requests / %d server syscalls)\n", d / n, d, n }'
    fi
  fi
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
  echo "harness: floor htgen -c$CONNS -d${DURATION}s reps=$REPS transport=$TRANSPORT path=$PATH_ASKED arm=${ARM:-kernel} nice=${NICE:-default} $MEMLOCK_LINE cflags=$CFLAGS_LINE $(uname -mr)"
  printf 'rps:'
  printf ' %s' "${RPS[@]}"
  printf '\n'
  echo "median rps: $MEDIAN over $REPS run(s), range ${SPREAD}% of the median"
  [ "$BOTH_PEGGED" = 1 ] && echo "both ends pegged - the scheduler decided the split"
  echo
} >> "$here/bench/results/floor.log"
