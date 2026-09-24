# memcpy by where source and target lie to each other

2026-09-24, on the host (bench/measure_on_host.sh: uid 65534, nice -10, cores 1-3,
core 0 left over), -O2 -march=x86-64-v4, one clock, every arm its own
shared object, random interleaving, medians; the empty arm beside every
set is the noise floor, 2.4 to 3.2 ns. Host: Intel Xeon 2.80GHz, 4 cpus,
cascadelake, L1d 32 KiB, L2 4 MiB, L3 33 MiB, Linux 6.18.44.

One mmap of 4 MiB, source and target at fixed offsets in it; filled at
load. Once with default pages, once with
MADV_HUGEPAGE after the mapping was populated, so huge pages are not
certain there; not checked. Thirty repetitions.

| target against source           | 8184 B, g++ / clang | 16376 B, g++ / clang |
|---------------------------------|---------------------|----------------------|
| after, distance 64 mod 4096     | 86.9 / 86.1 ns      | 169.2 / 175.7 ns     |
| before, distance 64 mod 4096    | 103.2 / 100.1 ns    | 147.6 / 151.0 ns     |
| after, distance 16 whole pages  | 85.5 / 86.3 ns      | 142.9 / 146.4 ns     |
| before, distance 16 whole pages | 89.5 / 84.6 ns      | 149.3 / 154.2 ns     |
| after, distance 2048 mod 4096   | 103.0 / 100.7 ns    | 131.7 / 139.6 ns     |
| both one byte off, after        | 86.4 / 85.7 ns      | 170.8 / 175.0 ns     |

Spread 2 to 12 percent, one arm under g++ 23 percent. The huge page
advice changed no row beyond the spread.

What it says: placement moves a copy by 15 to 25 percent, and which
placement is slow depends on the size. A distance of whole pages is
among the fastest at both sizes and in both directions; buffers carved
from one mapping in page steps always have it.
