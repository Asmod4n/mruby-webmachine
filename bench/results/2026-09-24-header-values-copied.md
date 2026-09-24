# Header values as fresh Ruby strings or as views into one copy of the head

2026-09-24, on the host (bench/measure_on_host.sh: uid 65534, nice -10, cores 1-3,
core 0 left over), -O2 -march=x86-64-v4, one clock, every arm its own
shared object, random interleaving, medians; the empty arm beside every
set is the noise floor, 2.4 to 3.2 ns. Host: Intel Xeon 2.80GHz, 4 cpus,
cascadelake, L1d 32 KiB, L2 4 MiB, L3 33 MiB, Linux 6.18.44.

Input: the 663 byte head of a browser GET; the field places are known
before the arm runs. Each call builds one Hash with symbol keys; fresh
means mrb_str_new from the receive buffer per value, view means one
mrb_str_new of the whole head and mrb_str_byte_subseq per value. Twenty
repetitions.

| arm                          | g++ 16    | clang 23  |
|------------------------------|-----------|-----------|
| empty Hash only              | 78.0 ns   | 75.2 ns   |
| 4 fields, fresh              | 416.0 ns  | 406.5 ns  |
| 4 fields, views              | 425.6 ns  | 405.0 ns  |
| 14 fields, fresh             | 1492.0 ns | 1555.7 ns |
| 14 fields, views             | 1386.6 ns | 1410.1 ns |

Spread 3 to 7 percent, the empty Hash under clang 14 percent.

What it says: at 4 fields the two are equal. At 14 fields the views are
7 to 9 percent faster, about twice the spread. A field costs 85 to 105 ns
either way: the object and the Hash entry, not the bytes. Values up to
27 bytes are embedded in both arms.
