# How a number is made here

    rake bench

It builds `bench/*.cpp` against `src/`, runs every case five times, and
writes `bench/results/<timestamp>.json`.

## Reading the numbers

`_median` is the number. `_cv` beside it is the spread, and a number
whose spread is larger than the difference you are looking at says
nothing. On the machine that wrote the first files here, `_cv` sits
between 0.1 and 2 percent.

## Comparing before against after

Google Benchmark ships the tool:

    compare.py benchmarks bench/results/<before>.json bench/results/<after>.json

Compare two runs from one session on one machine. Two files from two
machines, or from two days, describe two different things, whatever
their names say.

## What each file records about where it came from

The `context` block holds the answer, and every field in it is there
because a number was once read wrong without it.

- `commit` names the state of the tree, and `-dirty` says the code
  was not committed.
- `march` says what `-march=native` resolved to. Two runs in two
  containers can land on different hardware, and then the two arms are
  two binaries and not two versions.
- `compiler`, `libstdcxx` and `libc` are read off the binary and off
  the libraries it will load, not off `PATH`. A host that updated its
  packages between two runs is otherwise invisible.
- `on` answers virtual machine and container separately. A container
  in a virtual machine and one on metal read different rates.
- `scheduler` names the sched_ext scheduler, which is loaded at run
  time and swapped without a reboot. The kernel name does not answer
  it, and the same binary reads differently under two schedulers.
- `cpu_flag_count` and `cpu_flag_digest` stand in for ninety two flag
  names. A difference in any one of them changes what the compiler
  emitted.
