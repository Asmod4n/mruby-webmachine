# Handing a method a callback

2026-09-24, on the host (bench/measure_on_host.sh: uid 65534, nice -10, cores 1-3,
core 0 left over), -O2 -march=x86-64-v4, one clock, every arm its own
shared object, twenty repetitions, random interleaving, medians; the
empty arm beside them is the noise floor, 2.5 to 2.9 ns. Host: Intel
Xeon 2.80GHz, 4 cpus, cascadelake, Linux 6.18.44.

Each call runs 100 times; the numbers are per call. mruby-method is in
the build for these rows.

| callback                                   | g++ 16 | clang 23 |
|--------------------------------------------|--------|----------|
| none, the method called directly           | 57 ns  | 66 ns    |
| a Proc made once in the class body         | 98 ns  | 109 ns   |
| instance_method once, then bind_call       | 109 ns | 140 ns   |
| a block literal on every call              | 127 ns | 146 ns   |
| method(:x) on every call, then call        | 419 ns | 442 ns   |

Spread 3 to 12 percent. send(:x) is not measured: it comes with
mruby-metaprog, which this server does not load.

What it says: Kernel#method on every call costs three to four times a
Proc made once, because it builds a Method object and searches every
time. A block literal is a new RProc on every call, checked by address:
two calls gave two procs over one irep; a Proc in the class body kept
its address through a full GC, and freezing it held.
