# memcpy by size, at powers of two less the 8 bytes glibc malloc keeps

2026-09-24, on the host (bench/measure_on_host.sh: uid 65534, nice -10, cores 1-3,
core 0 left over), -O2 -march=x86-64-v4, one clock, every arm its own
shared object, random interleaving, medians; the empty arm beside every
set is the noise floor, 2.4 to 3.2 ns. Host: Intel Xeon 2.80GHz, 4 cpus,
cascadelake, L1d 32 KiB, L2 4 MiB, L3 33 MiB, Linux 6.18.44.

glibc in the container takes n + 8 bytes rounded up to 16 for a malloc
of n: up to 4088 bytes asked fill one 4096 byte chunk, 4096 asked take
4112. Source and target are static, source filled at load with bytes
the compiler cannot know; the size reaches memcpy at run time. Thirty
repetitions.

| bytes | g++ 16    | clang 23  |
|-------|-----------|-----------|
| 8     | 5.2 ns    | 5.4 ns    |
| 24    | 5.1 ns    | 5.4 ns    |
| 56    | 4.6 ns    | 4.8 ns    |
| 120   | 6.1 ns    | 5.6 ns    |
| 248   | 6.7 ns    | 8.4 ns    |
| 504   | 10.0 ns   | 11.0 ns   |
| 1016  | 16.0 ns   | 15.9 ns   |
| 2040  | 28.3 ns   | 23.6 ns   |
| 4088  | 53.2 ns   | 50.8 ns   |
| 8184  | 102.1 ns  | 82.8 ns   |
| 16376 | 147.0 ns  | 171.0 ns  |
| 32760 | 1250.0 ns | 1256.6 ns |
| 65528 | 2549.5 ns | 2550.8 ns |

Spread 1 to 7 percent up to 16376 bytes, 10 to 16 percent above.

What it says: below 128 bytes a copy costs its call, 5 ns. Up to 16376
bytes source and target stay in L1 and a copy runs at 75 to 110 GB/s.
At 32760 they do not, and a copy of twice the bytes costs eight times
as long, 26 GB/s. The two compilers differ where the layout of source
and target differs: both call the same glibc memcpy.
