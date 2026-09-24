# Keeping a value for one request between two resource methods

2026-09-24, on the host (bench/measure_on_host.sh: uid 65534, nice -10, cores 1-3,
core 0 left over), -O2 -march=x86-64-v4, one clock, every arm its own
shared object, twenty repetitions, random interleaving, medians; the
empty arm beside them is the noise floor, 2.5 to 2.9 ns. Host: Intel
Xeon 2.80GHz, 4 cpus, cascadelake, Linux 6.18.44.

Each call runs 100 pairs: one method stores a value, a second reads it.

| arm                                      | g++ 16   | clang 23 |
|------------------------------------------|----------|----------|
| two method calls, nothing kept           | 8955 ns  | 9962 ns  |
| @x on the resource                       | 10001 ns | 11870 ns |
| request.userdata, attr_accessor in Ruby  | 13742 ns | 15728 ns |
| request.userdata, a C method, mrb_iv_set | 16041 ns | 18148 ns |

Spread 4 to 15 percent.

What it says: an instance variable of the resource costs 10 to 19 ns a
pair over the two calls; Ruby's own attr_accessor 48 to 58 ns; a C
method 71 to 82 ns, because it goes through mrb_get_args. A value
cannot sit in a C struct behind an mruby Data object: mruby gives Data
no mark hook, so the GC would not keep it.
