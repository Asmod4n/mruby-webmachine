#!/bin/sh
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
CPUS=$(( $(nproc) - 1 ))
exec podman run --rm --network=none --cap-add=SYS_NICE --cpus="$CPUS" \
    -v "$HERE":/work:ro,Z -w /work localhost/compilers:sid \
    sh -c 'nice -n -10 setpriv --reuid=65534 --regid=65534 --clear-groups --inh-caps=-all \
        sh -c "echo \"run: uid \$(id -u) nice \$(nice) cpus $0\" >&2; exec \"\$@\"" run "$@"' \
    "$CPUS" "$@"
