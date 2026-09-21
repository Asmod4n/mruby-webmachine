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

## Counting instructions where callgrind cannot

`bench/instructions.sh` runs callgrind, and callgrind refuses
`pcmpestri` in either encoding. picohttpparser uses it, so any arm that
reads a head through picohttpparser dies with SIGILL. This machine has
no performance counters either, so `perf stat` reports `<not supported>`
for `instructions`.

qemu counts what both cannot, and the qemu-user that Ubuntu ships is
built without plugin support. Build one that has it:

    ./configure --target-list=x86_64-linux-user --enable-plugins \
                --disable-docs --disable-tools --disable-system
    make -j"$(nproc)"

Then take the difference of two run lengths, the way instructions.sh
does:

    qemu-x86_64 -plugin build/tests/tcg/plugins/libinsn.so -d plugin \
        bench/run '--benchmark_filter=^<arm>$' --benchmark_min_time=400x

The last line of the output reads `total insns: <count>`. Run the arm at
400 and at 800 iterations and divide the difference by 400.

Read the number beside a time, never instead of one. picohttpparser's
`pcmpestri` reads sixteen bytes per instruction and costs many cycles
for it, so an arm can hold fewer instructions than another and still
take half again as long.
