#!/bin/sh
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
LAST=$(( $(nproc --all) - 1 ))
exec nice -n -10 setpriv --reuid=65534 --regid=65534 --clear-groups --inh-caps=-all \
    taskset -c "1-$LAST" \
    sh -c 'echo "run: uid $(id -u) nice $(nice) io_uring_disabled $(cat /proc/sys/kernel/io_uring_disabled) cpus $(taskset -pc $$ | sed "s/.*: //")" >&2
           loader=$0; libraries=$1; shift; exec "$loader" --library-path "$libraries" "$@"' \
    "$HERE/build/runtime/ld-linux-x86-64.so.2" "$HERE/build/runtime" "$@"
