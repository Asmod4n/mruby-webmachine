# The clock finds a known difference; a link does not move the number

2026-09-23, container localhost/compilers:sid (g++ 16.2.0, clang 23.1.2)
and the host (g++ 13, clang 18), -march=x86-64-v4, one clock
(bench/measure), every arm its own shared object, ten repetitions,
random interleaving, medians. Host: Intel Xeon 2.80GHz, 4 cpus.

Arm: std::count of one byte over N bytes.

Host, g++ 13 -O2, same arm at four lengths, alternating:
4096 2360 ns (cv 0.6%), 4137 +1.7% (0.9%), 4219 +3.2% (0.5%), 4506
+10.7% (6.9%). Sequential instead: +1.0%, +3.0%, +9.6%, first arm cv 9%.

Host, one source, four links at 4096: plain 2425, a copy of plain 2448,
alignment flags 2407, clang 18 400 ns. The three gcc links agree within
their spread: with dlopen the link does not make the number.

Container, g++ 16 -O2, same arm: 4096 304 ns, 4137 +8%, 4219 +13%,
4506 +15%. The loop is vectorized there and the time is not linear in
the bytes: the tail after the last full block is counted one byte at a
time. A byte loop is not a linear yardstick once it is vectorized.

Container, one source, six builds at 4096: g++ -O2 305, clang -O2 395,
clang -Os 845, g++ -Os 2593, clang -O1 2612, g++ -O1 2822 ns. At -Os
clang emits a vector loop (26 vector instructions, run 529 bytes) and
g++ counts bytes one at a time (run 322 bytes).
