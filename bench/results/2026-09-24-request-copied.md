# Reading a request in place against copying every request into a std::string

2026-09-24, container localhost/compilers:sid (g++ 16.2.0, clang 23.1.2),
-O2 -march=x86-64-v4, one clock (bench/measure), every arm its own
shared object, ten repetitions, random interleaving, medians. Host:
Intel Xeon 2.80GHz, 4 cpus, cascadelake, Linux 6.18.44.

Input per call as in 2026-09-24-stream-256-connections.md: 256
connections, four full buffers each, 119808 requests of 35 bytes. The
work per request is the same in both arms: find the head end, then
read the request-line and count the field lines. In place, only the
request across a buffer edge goes through the connection's carry. The
copied arm assigns every request into a std::string kept per
connection, capacity kept, and reads it there. Both arms answered
1434789.

| arm                                   | g++ 16   | clang 23 |
|---------------------------------------|----------|----------|
| read in place                         | 5.80 ms  | 4.81 ms  |
| every request copied into std::string | 7.64 ms  | 6.94 ms  |

Spread 2 to 8 percent. The copy costs 32 percent under g++ and 44
percent under clang: 15 ns a request in place, 18 ns more copied, even
with no allocation, because the 35 bytes are written once more and
read from a second place.
