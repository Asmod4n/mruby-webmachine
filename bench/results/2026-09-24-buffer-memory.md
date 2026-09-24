# What memory backs the receive buffers

2026-09-24, container localhost/compilers:sid, g++ 16.2.0, -O2
-march=x86-64-v4, one clock (bench/measure), every arm its own shared
object, ten repetitions, random interleaving, medians. Host: Intel Xeon
2.80GHz, 4 cpus, cascadelake, Linux 6.18.44, transparent hugepages
"always".

Seven kinds of memory, each giving 1024 buffers of 4096 bytes, 4 MiB,
read as the stream of 256 connections (2026-09-24-stream-256-connections.md):
find inside each buffer with a state per connection.

Steady: the memory is taken and touched once, every call reads it.

| memory                         | one round |
|--------------------------------|-----------|
| std::vector<char>, one block   | 1.901 ms  |
| malloc, one block              | 1.907 ms  |
| static array                   | 1.916 ms  |
| mmap + MADV_HUGEPAGE           | 1.922 ms  |
| aligned_alloc 4096             | 1.923 ms  |
| std::vector<std::string>       | 1.928 ms  |
| mmap anonymous                 | 1.956 ms  |

Spread 0.7 to 2.5 percent. All seven lie within the spread: once the
pages exist, the kind of memory does not show in the read.

Fresh: every call takes the memory, touches all 4 MiB, reads once and
gives it back.

| memory                         | one round |
|--------------------------------|-----------|
| static array                   | 4.87 ms   |
| aligned_alloc 4096             | 4.91 ms   |
| std::vector<char>              | 5.08 ms   |
| malloc                         | 5.09 ms   |
| std::vector<std::string>       | 5.30 ms   |
| mmap + MADV_HUGEPAGE           | 5.40 ms   |
| mmap anonymous                 | 6.83 ms   |

The difference is the page faults of first touch: a fresh mmap pays one
for each 4 KiB page, the hugepage advice fewer, and the allocators reuse
memory they already touched. That is paid once at start for a pool
that is taken once.

Not measured: a buffer that is an mruby string (libmruby is not built
in this tree today); the kernel's copy into the buffer on recv, which
the clock cannot see without a socket.
