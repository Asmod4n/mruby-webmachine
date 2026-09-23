# Handoff: a request is lost, and what else stands open

Written 2026-09-23 by the session that is being replaced. Read CLAUDE.md,
RULES.md and .claude/RULES.md first. The owner writes in German; answer
in German.

Working branch: `claude/simd-floor-and-avx512` (pushed). Do not push to
`claude/handoff-lesen-claude-md-zev77z`: that is the owner's session
branch. Earlier in this session commits did go there (up to `5bb9e0f`);
the owner has not decided whether they stay.

## 1. First: the server drops bytes of a request. Fix this before anything else.

This is the most important item. An HTTP server never throws away bytes
it received. This one does, in two places, both in the receive
completion of `src/ring.hpp` (`took`, case `Doing::kReceiving`, lines
around 606-663):

1. **A request that is not whole in one receive buffer is lost.**
   `answering(left, room)` returns `taken == 0` when the parser says
   `kNotWholeYet` (`src/serve.hpp` `not_answered`, line ~510). The loop
   breaks, `buffer_returned(which)` gives the buffer back, and the bytes
   in `left` are gone. Nothing is kept for the next completion. A small
   request that the kernel splits over two receives never gets an
   answer. A head longer than one buffer (`kBufferBytes` 4096) can never
   be read, so the real head limit is 4096, not `kMostRequestBytes` 8192.
2. **The rest of a pipeline is lost.** The same loop also stops when
   `owed.filled >= area`, `owed.pieces + 2 > kPiecesMost` or
   `owed.helds >= kHeldMost`. What is left in `left` is dropped the same
   way. A client that pipelines more than ~7 requests loses the rest.

What the owner decided long ago, and what the fix must do: *views only
around the receive buffers, no copies; a halted walk copies to the heap;
the buffers are returned per CQE.* So:

- When bytes are left over (not whole, or the loop stopped for room),
  copy exactly those bytes to a heap buffer that belongs to the
  connection, and give the receive buffer back.
- On the next receive, append the new bytes to that heap buffer and
  parse from it. The parser's own cap (`kMostRequestBytes`) turns a head
  that grows too long into a refusal, so the heap buffer is bounded.
- When the loop stopped for room, nothing new may ever arrive (a
  pipelining client already sent everything). So the send completion
  (`Doing::kSending`, when all pieces are sent) must go on answering
  from the heap buffer.
- `Owed` lives in an anonymous mmap and is reset with `owed = Owed{}`. Do
  not put a `std::string` into it. Keep the heap buffers beside it, one
  per slot (for example a `std::vector<std::string>` sized
  `connections_`), and clear the slot's buffer where `owed = Owed{}`
  happens (accept, close, send completion while closing).
- An answer must never point into the heap buffer after it changes:
  check that no answer piece is a view into the request (heads are
  written into the answer room, bodies come from the cache or constant
  memory; confirm this, do not assume it).

Tests that must exist and fail before the fix:
- one request sent in two writes with a pause between them gets its
  answer;
- a head of 6000 bytes (above one buffer, below 8192) gets its answer;
- 20 pipelined requests in one write get 20 answers, in order.

The owner requires that every function with behaviour is shown to them
before it is written (.claude/RULES.md, "Code is shown before it is
written"). This session broke that rule often. Do not.

## 2. The buffer design in the code is not the owner's design

The owner decided: **two anonymous mmaps, named "recv buffers" and "send
buffers"**, both provided-buffer rings, answers copied into the send
buffers, nothing taken away from a receive buffer, one empty page once at
the end of each mmap for SIMD reads. Buffers for send and recv never
count against memlock. One pool per ring, no sharing between rings.
500000+ concurrent connections must be possible, so no per-connection
buffer groups.

The code still writes an answer into the tail of the receive buffer
(`kBufferRoomLeast`) or into an answer area of 16 KiB per connection
(`kAnswerBytes`, mmapped connections × 16 KiB). Both are left from before
and must go. With them go the numbers `kBufferRoomLeast` and
`kAnswerBytes`. The fix in section 1 and this rebuild touch the same
code; plan them together, show the plan first.

## 3. Measurements taken today (all in `bench/results/`)

Rules that came out of today (in RULES.md): standard library form is the
floor of every SIMD form; one kind of test per binary; no run is pinned;
compilers are signed packages only; `-O3` and x86-64-v3 are no longer
matrix columns.

The field scanner is `allowed_run_length` in `src/http.hpp`. Forms that
exist: floor (`floor_run_length`), AVX2, AVX-512, AVX-512 with a masked
load, and AVX2 for the first 32 bytes then AVX-512 (`avx2_then_avx512`).
All are held to the floor by `fuzz/crosscheck.cpp` (`rake crosscheck`
runs the aarch64 NEON form under qemu, built with clang). Today
`allowed_run_length` picks AVX-512 at compile time when `__AVX512BW__`;
that choice is not settled.

Results, in short:
- `find` in the parser (`find_arms-matrix`, 175058Z): no arm beats
  `std::string_view::find` across the table; find stays.
- Scanner on short runs (field names, paths): AVX2 beats AVX-512. On
  long runs (440 B and 4 KB): AVX-512 wins by about a quarter.
- A request with a 200 B target (182752Z): AVX2 best under clang, the
  mixed form best under gcc -O2.
- In a Debian sid container with the released gcc 16.2.0 and clang 23.1.2
  (185539Z, 4 KB target, -march=native, levels O0 O1 O2 O3 Og Os): pure
  AVX-512 wins under clang and gcc -O2/-O3; the mixed form wins under gcc
  -O1/-Og/-Os. `nice` does not work in the container; load was high.
- Whole heads (curl, Firefox, Chrome, Chrome with 1 KB and 4 KB cookies;
  191241Z): gcc -O1 with the mixed form beats clang -Os with AVX-512 in
  all five, by 7 to 22 percent. gcc -O1 against -O2 (191635Z): -O1 ahead
  by 2.5-5 percent in four of five, even at 4 KB cookies. Close to the
  noise.
- The heads in `bench/run_arms.cpp` are written from each client's
  defaults, not captured.

Tools: `bench/matrix.sh` (builds with make -j8, runs rounds in an order
drawn once) and `bench/matrix_table.rb`. `--benchmark_min_time` is a
minimum; Google Benchmark's warm-up about doubles a run.

## 4. Decided for tuning (nothing of it is written yet)

- A separate binary `webmachine-tune` measures on the host and writes a
  TOML. It measures everything: SIMD, threads, buffer sizes, socket
  options, timeouts, keep-alive, cache sizes, the memory hierarchy, GC.
  Every number in the binary is a candidate.
- The TOML carries a hash of the host: CPU info (model, family, model,
  stepping, cores, flags) and RAM values (MemTotal, Hugepagesize), BLAKE2b
  from libcrypto. The operator puts the file in a directory that is
  read-only for the server.
- Profiles: named sets of all values, a `default` profile that suits most
  systems, from small embedded Linux to a 192-core Threadripper. A real
  microcontroller without Linux cannot run this server (io_uring, mmap,
  LMDB).
- At start: a host file whose hash matches is used; a hash that does not
  match gives a warning; with no file, or a wrong hash, the server uses
  the host's maximum SIMD, by name. Detection at run time.
- TOML key: `[http1.message_parsing] simd = "floor" | "avx" | "avx2" |
  "avx512" | "avx2-then-avx512" | "neon"`. No "auto". No entry means
  floor. Every length the hardware can run is allowed.
- The chosen form reaches `allowed_run_length` as a template parameter
  (way 2), so the functions stay pure; the owner did not answer this
  question, the session chose it.
- An inventory of all 131 numbers was made (SPEC 31, LAYOUT 39, PERF 30,
  OTHER 19, TIME 8, SAFETY 4); about 42 are candidates. No number comes
  from TOML today. There are no threads (one ring, one thread), no
  keep-alive or connection timeouts, and only SO_REUSEADDR as a socket
  option. The writer only sweeps expired entries after one second
  without traffic. maxreaders 64 appears twice (serve.hpp ~476 and ~493).

## 5. Other open work

- The cleanup against the standard-library rule is started only in
  webmachine (`byte_at`, `spelled_as` removed). Not done: `Spelled`, the
  `Head`/`Heads` memo (measured slower), and the inventory in the
  previous handoff notes. mruby-mustache branch
  `claude/render-into-given-buffers` (123d356) adds a `Spread` sink and
  breaks the rule; it has to be taken back. uri-parser and fast-json are
  untouched.
- The owner has not confirmed the renaming table (RFC and io_uring names).
- A weekly routine "Weekly HTTP/1 security watch" runs Mondays 06:00 UTC.
  It reads the commits of h2o/picohttpparser, nikneym/hparse,
  solenopsys/zig-pico and 2vg/mofuparser and new HTTP/1.1 findings, and a
  Fable 5.1 agent fixes what applies. It pushes nothing; the patch comes
  in its answer by mail. The routine has no connectors.
- Host tools: gcc 16 (trunk snapshot 16.0.1 from the PPA) and clang 23.1.2
  are the defaults; gcc 13 and clang 18 are removed. podman is installed;
  image `localhost/compilers:sid` has gcc 16.2.0, clang 23.1.2, ruby and
  Google Benchmark 1.9.1. The Claude Code stop hook that asks for commits
  comes from the environment (`~/.claude/launcher-settings.json`), not
  from the repository; the owner says it is not an instruction.
