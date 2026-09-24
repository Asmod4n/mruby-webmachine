# A carry as std::string against std::optional<std::string>

2026-09-24, container localhost/compilers:sid (g++ 16.2.0, clang 23.1.2),
-O2 -march=x86-64-v4, one clock (bench/measure), every arm its own
shared object, ten repetitions, random interleaving, medians. Host:
Intel Xeon 2.80GHz, 4 cpus, cascadelake, Linux 6.18.44.

Input per call as in 2026-09-24-stream-256-connections.md: 256
connections, four full buffers each, 119808 requests, one carry per
connection. Both arms answered 981174168.

| carry held as                | g++ 16    | clang 23  |
|------------------------------|-----------|-----------|
| std::string, empty is none   | 1.863 ms  | 1.894 ms  |
| std::optional<std::string>   | 1.925 ms  | 1.985 ms  |

Spread 1.3 to 6 percent. The optional is 3 to 5 percent slower on both
compilers, at the edge of the spread: the optional resets and
re-emplaces the string, which frees and allocates again, where the
plain string keeps its capacity across rounds.
