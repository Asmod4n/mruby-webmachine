# Carry through a per-thread area against a state at the edge

2026-09-24, container localhost/compilers:sid (g++ 16.2.0, clang 23.1.2),
-O2 -march=x86-64-v4, one clock (bench/measure), every arm its own
shared object, ten repetitions, random interleaving, medians. Host:
Intel Xeon 2.80GHz, 4 cpus, cascadelake, Linux 6.18.44.

Input per call, as in 2026-09-24-heads-across-buffers.md: 128 requests
of 35 bytes, 4480 bytes over two buffers of 4096, the 118th request
across the edge, 385 bytes carried. Every arm answered 288448.

| arm                                         | g++ 16  | clang 23 |
|---------------------------------------------|---------|----------|
| find inside each buffer, a state at the edge| 1690 ns | 1675 ns  |
| carry as a std::string                      | 1680 ns | 1751 ns  |
| carry through a thread_local array          | 1701 ns | 1714 ns  |
| carry through a malloc once per thread      | 1681 ns | 1712 ns  |
| carry through an array on the stack         | 1699 ns | 1721 ns  |

Spread 0.5 to 8 percent. All five lie within the spread of each other.

What it says: at this shape the copy of 385 bytes is not visible beside
128 finds, whatever area it goes to. The choice between a carry and a
state at the edge is not a speed question here. A shape with few
requests per bundle, or a long head across the edge, was not measured.
