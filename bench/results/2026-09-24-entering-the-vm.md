# Entering the VM from C++ through mrb_protect_error

2026-09-24, container localhost/compilers:sid, -O2 -march=x86-64-v4,
the measure clock with the server linked in, every arm its own shared
object, ten repetitions, random interleaving, medians. Host: Intel
Xeon 2.80GHz, 4 cpus, cascadelake, Linux 6.18.44.

Every arm calls `mrb_funcall_argv` on `def pass(x) = x` with one
fixnum. The mrb_state is opened once per arm. All arms answered 1.

| form                                   | g++ 16   | clang 23 |
|----------------------------------------|----------|----------|
| empty run, the price of the clock      | 2.8 ns   | 2.4 ns   |
| direct, no mrb_protect_error           | 53.6 ns  | 60.8 ns  |
| named function and a struct            | 52.3 ns  | 58.6 ns  |
| lambda without capture, `+[]`          | 52.7 ns  | 60.0 ns  |
| template trampoline, `[&]`             | 54.4 ns  | 58.8 ns  |
| template trampoline, named captures    | 54.7 ns  | 59.6 ns  |
| std::function_ref, `[&]`               | 54.9 ns  | 60.7 ns  |
| std::function_ref, named captures      | 55.3 ns  | 60.0 ns  |
| std::function, `[&]`                   | 69.3 ns  | 76.6 ns  |

Spread 1 to 16 percent. Every form compiled on both compilers.

Direct is a reference and not a form: without mrb_protect_error, an
exception ends in abort(). Against it, mrb_protect_error costs nothing
measurable on the path that succeeds.

The named function, the plain lambda, the template trampoline and
std::function_ref are equal within the spread, and `[&]` costs the
same as named captures. std::function is about 15 ns slower on both
compilers.
