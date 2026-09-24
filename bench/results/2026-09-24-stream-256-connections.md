# The receive stream of 256 connections, full bundles

2026-09-24, container localhost/compilers:sid (g++ 16.2.0, clang 23.1.2),
-O2 -march=x86-64-v4, one clock (bench/measure), every arm its own
shared object, ten repetitions, random interleaving, medians. Host:
Intel Xeon 2.80GHz, 4 cpus, cascadelake, Linux 6.18.44.

Input per call: one round of 256 connections, each one bundle of four
full buffers of 4096 bytes, 4 MiB in all, 119808 requests of 35 bytes,
every connection's stream at its own phase so requests lie across every
buffer edge, and the state of a connection carries from one round to
the next. Both arms answered 981174168.

| arm                                      | g++ 16     | clang 23   |
|------------------------------------------|------------|------------|
| find inside each buffer, a state per conn| 1.933 ms   | 1.992 ms   |
| carry as a std::string per connection    | 1.876 ms   | 1.879 ms   |

Spread 0.4 to 4 percent. 16 ns per request, 60 to 64 million requests
per second on one core, for finding the head ends alone. 100 million
per second is 1.6 cores of that work. The two arms are within their
spread of each other.

Not measured: the parse of the request-line and the fields, the walk,
the answer, and the ring. This is the floor of the receive side.
