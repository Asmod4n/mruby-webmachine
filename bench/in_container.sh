#!/usr/bin/env bash
# Run a command inside the compilers container, with this tree at /work
# and the working directory the same as here. Everything the command
# writes lands in the tree on the host.
#
#   bench/in_container.sh g++ -std=c++23 -Os -fPIC -shared bench/arm_bytes_counted.cpp -o build/arm.so
#   bench/in_container.sh bash
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
exec podman run --rm -it --network=host \
    -v "$HERE":/work:Z \
    -w "/work/$(realpath --relative-to="$HERE" "$PWD")" \
    localhost/compilers:sid "$@"
