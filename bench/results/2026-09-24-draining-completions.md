# Draining completions: for_each, peek, or both

2026-09-24, on the host (bench/measure_on_host.sh: uid 65534, nice -10,
cores 1-3, core 0 left over), -O2 -march=x86-64-v4, one clock, every arm
its own shared object, kernel 6.18.44 answering (engine 0). Host: Intel
Xeon 2.80GHz, 4 cpus, cascadelake.

Server and client are two threads with a ring each, over 256 socketpairs
registered as direct descriptors. Both receive with multishot recv and
IORING_RECVSEND_BUNDLE from provided buffers, 2956 of 4096 bytes each,
35 percent of the 33 MiB L3. The client filled all its send buffers
once and sends every free one per enter as send bundles. Buffers go
back once per batch; the kernel shortens a bundle's last entry before
7.1, so every entry is written back whole. ENOBUFS is push back: the
buffers go back, and recv is armed again only without F_MORE. A run is
10 seconds; a run where either side used under 90 percent of a core,
or ended on another negative res, is not counted.

| drain after submit_and_wait_timeout | g++ 16            | clang 23          |
|-------------------------------------|-------------------|-------------------|
| io_uring_for_each_cqe, cq_advance   | 6.01 M requests/s | 6.11 M requests/s |
| io_uring_peek_cqe, cqe_seen         | 6.56 M requests/s | 6.27 M requests/s |
| for_each, then peek                 | 5.95 M requests/s | 5.97 M requests/s |

Medians of four or five runs; the ranges are 13 to 20 percent wide and
overlap in every row. Three runs were not counted: the client got EPIPE
while the pairs were closed at the end.

What it says: no drain is faster. Bundles are what count: the same pair
without bundles answered about 1.75 M requests/s.

On a ring of 16 submission entries and 32 completion entries, get_sqe
answered NULL 6960 times under for_each and 14530 times under peek.
io_uring_submit succeeded every time, no EBUSY, no EAGAIN, and no
completion was lost. The kernel kept the ones that did not fit in its
overflow list: IORING_SQ_CQ_OVERFLOW was set at 97 percent of those
submits under for_each and at 82 percent under peek, because peek frees
every completion as it is read.

Decided: the server drains with peek. It frees each completion at once,
and it can stop in the middle of a batch.
