# Marking where each field starts while the head is parsed

2026-09-24, on the host (bench/measure_on_host.sh: uid 65534, nice -10, cores 1-3,
core 0 left over), -O2 -march=x86-64-v4, one clock, every arm its own
shared object, random interleaving, medians; the empty arm beside every
set is the noise floor, 2.4 to 3.2 ns. Host: Intel Xeon 2.80GHz, 4 cpus,
cascadelake, L1d 32 KiB, L2 4 MiB, L3 33 MiB, Linux 6.18.44.

Input: one browser GET, 663 bytes, 14 fields. Marked: per first letter
of the field name, up to four places of position and name length; only
the 32 counts are zeroed. Later lookups: user-agent, cookie,
accept-language and one field that is not there. Twenty repetitions.

| arm                                        | g++ 16   | clang 23 |
|--------------------------------------------|----------|----------|
| parse, nothing marked                      | 292.9 ns | 200.6 ns |
| parse and mark                             | 332.3 ns | 263.6 ns |
| parse and mark, the same arm again         | 338.8 ns | 257.4 ns |
| parse, then four lookups by scanning again | 1239.5 ns | 1108.5 ns |
| parse and mark, then four lookups by mark  | 463.3 ns | 333.4 ns |

Spread 3 to 10 percent; the two equal arms are 2 percent apart.

What it says: marking costs 40 ns under g++ and 60 ns under clang for
14 fields, 13 and 31 percent of the parse. A lookup by mark costs 30 to
40 ns, a lookup by scanning again about 230 ns. Marking pays once a
fifth of the requests look up one skipped field later.

Decided since: a resource names its fields once (request_headers), and
parse_remaining_headers reads the rest when asked; nothing is marked.
