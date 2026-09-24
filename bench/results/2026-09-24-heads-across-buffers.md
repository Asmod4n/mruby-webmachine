# Finding request heads across two receive buffers

2026-09-24, tree at b197a37 plus the arm, container localhost/compilers:sid
(g++ 16.2.0, clang 23.1.2), -O2 -march=x86-64-v4, one clock (bench/measure),
every arm its own shared object, ten repetitions, random interleaving,
medians. Host: Intel Xeon 2.80GHz, 4 cpus, cascadelake, Linux 6.18.44.

Input per call: 128 requests of 35 bytes, 4480 bytes over two buffers of
4096, the 118th request across the edge. This is the shape htgen
--pipeline 128 delivers per completion with IORING_RECVSEND_BUNDLE,
measured the same day. Every arm answered 288448.

| arm                                           | g++ 16  | clang 23 |
|-----------------------------------------------|---------|----------|
| find inside each buffer, a state at the edge  | 1668 ns | 1672 ns  |
| carry: find per buffer, the rest copied       | 1718 ns | 1719 ns  |
| copy: the bundle copied into one buffer, find | 1829 ns | 1705 ns  |
| state machine, one byte at a time             | 3010 ns | 3566 ns  |
| views::join with ranges::search               | 8722 ns | 12568 ns |

Spread 0.4 to 3 percent, the state machine under clang 11 percent.

What it says: find, vectorized, wins; a state is needed only at the
buffer edge, zero to three bytes of CRLF CRLF already seen; nothing is
copied. A byte-stepping state machine costs twice as much. views::join
costs five to seven times as much, because its iterator tests the inner
end at every byte.

Not measured: reading the request-line and the field lines of a head
that lies across the edge.
