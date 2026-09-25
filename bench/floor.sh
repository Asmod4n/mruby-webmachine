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
#   A run counts only when the server and the client each use at least
#   90 percent of one core. One below that was waiting on the other or
#   on the machine, and its number describes the wait. Such a run is
#   printed and marked, and it is not in the median.
#
#   One server thread, and one htgen with CLIENT_THREADS=1 or 2. Nothing
#   else runs beside them, and nothing larger is allowed.
#
#   ARCHIVE=path runs the archive's server in place of this one, with
#   APP= (the archive's bench/apps/hello.rb) compiled by MRBC=, so the
#   two are measured by the same lines, one at a time.
#
#   Every run prints the server's kernel share (stime over utime plus
#   stime) and its user time per request, and the medians are over the
#   runs that count.
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
CLIENT_THREADS="${CLIENT_THREADS:-1}"
case "$CLIENT_THREADS" in
  1|2) ;;
  *) echo "CLIENT_THREADS is 1 or 2: one server thread and one htgen of one or two threads, nothing more" >&2; exit 2 ;;
esac
ARCHIVE="${ARCHIVE:-}"

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

if [ -n "$ARCHIVE" ]; then
  APP_LABEL="$(basename "${APP:-app}" .rb)@archive:$(basename "$ARCHIVE")"
  [ "$TRANSPORT" = unix ] || { echo "ARCHIVE= runs on a UNIX socket only" >&2; exit 2; }
  [ -x "$ARCHIVE" ] && [ -f "${APP:-}" ] && [ -x "${MRBC:-}" ] || {
    echo "ARCHIVE= needs APP= (the archive's bench/apps/hello.rb) and MRBC= (the archive's mrbc)" >&2
    exit 2
  }
  { printf 'BENCH_LISTEN = { unix_path: "%s" }\n' "$SOCK"; cat "$APP"; } > "$WORK/app.rb"
  "$MRBC" -o "$WORK/app.mrb" "$WORK/app.rb" || exit 1
  : > "$WORK/none.toml"
  "${NICE_ASK[@]+"${NICE_ASK[@]}"}" "$ARCHIVE" --config="$WORK/none.toml" --app="$WORK/app.mrb" >"$WORK/srv.out" 2>&1 &
  SRV=$!
  WHERE=(--sock "$SOCK")
elif [ "$TRANSPORT" = unix ]; then
  APP_LABEL="$(basename "$SERVER")"
  "${NICE_ASK[@]+"${NICE_ASK[@]}"}" "$SERVER" "$SOCK" $ARM >"$WORK/srv.out" 2>&1 &
  SRV=$!
  WHERE=(--sock "$SOCK")
else
  APP_LABEL="$(basename "$SERVER")"
  "${NICE_ASK[@]+"${NICE_ASK[@]}"}" "$SERVER" "$PORT" $ARM >"$WORK/srv.out" 2>&1 &
  SRV=$!
  WHERE=(--host 127.0.0.1 --port "$PORT")
fi

for _ in $(seq 1 100); do
  if [ "$TRANSPORT" = unix ]; then [ -S "$SOCK" ] && break; else sleep 0.05; break; fi
  sleep 0.05
done
kill -0 "$SRV" 2>/dev/null || { echo "the server did not come up:" >&2; cat "$WORK/srv.out" >&2; exit 1; }
"$HTGEN" "${WHERE[@]}" --conns 1 --seconds 1 --path "$PATH_ASKED" >/dev/null 2>&1
sleep 1.2

CFLAGS_LINE=$(grep -o "'-[^']*'" "$here/build_config_release.rb" | tr -d "'" | sort -u |
  tr '\n' ' ' | sed 's/ $//')
echo "harness: floor app=$APP_LABEL ${ARCHIVE:+archive=$ARCHIVE }htgen -c$CONNS -t$CLIENT_THREADS -d${DURATION}s reps=$REPS transport=$TRANSPORT path=$PATH_ASKED arm=${ARM:-kernel} nice=${NICE:-default} $MEMLOCK_LINE cflags=$CFLAGS_LINE $(uname -mr)"

RPS=()
SHARES=()
USER_NS=()
INVALID=0
for rep in $(seq 1 "$REPS"); do
  M0=$(machine_busy)
  read -r SU0 SS0 <<<"$(ticks_of "$SRV")"
  sysc_begin "$SRV" "$DURATION"
  "$HTGEN" "${WHERE[@]}" --conns "$CONNS" --threads "$CLIENT_THREADS" --seconds "$DURATION" --path "$PATH_ASKED" \
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

  NDONE=$(grep -o 'responses=[0-9]*' "$WORK/cli.out" | cut -d= -f2)
  SHARE=$(awk -v u=$((SU1 - SU0)) -v s=$((SS1 - SS0)) 'BEGIN { printf "%.1f", ((u + s) > 0 ? s * 100 / (u + s) : 0) }')
  UNS=$(awk -v u=$((SU1 - SU0)) -v k="$HZ" -v d="$NDONE" 'BEGIN { printf "%.0f", (d > 0 ? u / k * 1e9 / d : 0) }')

  cat "$WORK/cli.out"
  if [ "$SCPU" -ge 90 ] && [ "$CCPU" -ge 90 ]; then
    COUNTS=counts
  else
    COUNTS="does not count: under 90% of a core"
    INVALID=$((INVALID + 1))
  fi
  echo "server: ${SCPU}% of one core   client: ${CCPU}% of one core   other: ${OTHER}% of one core   kernel share: ${SHARE}%   user per request: ${UNS} ns   ${COUNTS}"
  [ "$COUNTS" = counts ] || continue
  if [ -n "$SYSC_PERF" ]; then
    [ -n "$SYSC_PID" ] && wait "$SYSC_PID" 2>/dev/null
    SYSC_PID=
    NSYSC=$(sysc_read)
    if [ -n "$NSYSC" ] && [ "$NSYSC" -gt 0 ] && [ -n "$NDONE" ]; then
      awk -v d="$NDONE" -v n="$NSYSC" 'BEGIN { printf "req/syscall: %.1f (%d requests / %d server syscalls)\n", d / n, d, n }'
    fi
  fi
  RPS+=("$(grep -o 'rps=[0-9]*' "$WORK/cli.out" | cut -d= -f2)")
  SHARES+=("$SHARE")
  USER_NS+=("$UNS")
done

[ "${#RPS[@]}" -gt 0 ] || { echo "no run counts: $INVALID of $REPS were under 90% of a core" >&2; exit 1; }
median_of() {
  printf '%s\n' "$@" | sort -n | awk '{ a[NR] = $1 } END { print (NR % 2) ? a[(NR + 1) / 2] : (a[NR / 2] + a[NR / 2 + 1]) / 2 }'
}

MEDIAN=$(printf '%s\n' "${RPS[@]}" | sort -n | awk '{ a[NR] = $1 }
  END { print (NR % 2) ? a[(NR + 1) / 2] : int((a[NR / 2] + a[NR / 2 + 1]) / 2) }')
SPREAD=$(printf '%s\n' "${RPS[@]}" | sort -n | awk -v m="$MEDIAN" '{ a[NR] = $1 }
  END { printf "%.1f", (m > 0 ? (a[NR] - a[1]) * 100 / m : 0) }')
COUNTED="${#RPS[@]} of $REPS run(s) count"
echo "median rps: $MEDIAN, $COUNTED, range ${SPREAD}% of the median"
echo "median kernel share: $(median_of "${SHARES[@]}")%   median user per request: $(median_of "${USER_NS[@]}") ns"

mkdir -p "$here/bench/results"
{
  echo "harness: floor app=$APP_LABEL ${ARCHIVE:+archive=$ARCHIVE }htgen -c$CONNS -t$CLIENT_THREADS -d${DURATION}s reps=$REPS transport=$TRANSPORT path=$PATH_ASKED arm=${ARM:-kernel} nice=${NICE:-default} $MEMLOCK_LINE cflags=$CFLAGS_LINE $(uname -mr)"
  printf 'rps:'
  printf ' %s' "${RPS[@]}"
  printf '\n'
  echo "median rps: $MEDIAN, $COUNTED, range ${SPREAD}% of the median"
  echo "median kernel share: $(median_of "${SHARES[@]}")%   median user per request: $(median_of "${USER_NS[@]}") ns"
  echo
} >> "$here/bench/results/floor.log"
