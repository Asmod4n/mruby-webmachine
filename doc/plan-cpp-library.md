# Plan: webmachine as a C++ library

Status: draft 3, WASI dropped (owner, 2026-11-02). Branch `plan-cpp-library`,
from `integration`.

## 1. Goal

The tree holds no mruby. Everything is C++. Other languages use the
library through bindings that reflection makes from the C++ types.
There is no C API.

Decisions of the owner that this plan takes as given (2026-11-02):

- mruby leaves the tree.
- There is no C API.
- A dependency is fetched by CMake where possible. CRC32C, libhydrogen
  and LMDB come as submodules (RULES.md).
- clang has no reflection (clang 23.1.3 rejects `^^` and defines no
  `__cpp_impl_reflection`, checked). There Boost.PFR 1.84 or later reads
  the members.

## 2. What the tree holds today

Read on `integration` (8c76f51 and later).

| part | mruby |
|---|---|
| `src/webmachine.cpp` (16 lines) | the gem init and final |
| `src/config.hpp` | reads TOML through the gem `mruby-toml`, 29 `mrb_` uses |
| `src/problem.hpp`, `src/walk.hpp`, `src/home.hpp` | `MRB_DEBUG` switches debug code |
| `src/home.hpp:43,65` | the text mruby-webmachine, stays until the rename |
| `tools/*/main.cpp` | built as gem binaries |
| `test/*.rb` | 221 `assert(` blocks: config 4, http 187, problem 22, walk 8 |
| `test/*.hpp`, `test/bindings.cpp` | 1698 lines of mruby bindings for the tests |
| `Rakefile` | tasks `test`, `bench`, `fuzz`, `crosscheck`, `install` |
| `bench/*.sh`, `bench/*.cpp`, `fuzz/` | take ada, LMDB and mustache from `mruby/build` |
| `mrbgem.rake`, `build_config_*.rb` | the build |

Dependencies, and how they arrive:

| dependency | today | after this plan |
|---|---|---|
| mruby-c-ext-helpers | gem | removed with mruby |
| ada (URL) | gem `mruby-uri-parser` | CMake `FetchContent` |
| LMDB | gem `mruby-lmdb` | submodule (RULES.md) |
| CRC32C | written by hand in `src/cache.c`, `#error` on other architectures | submodule (RULES.md) |
| simdjson | gem `mruby-fast-json` | CMake `FetchContent` |
| TOML | gem `mruby-toml` (toml11 inside) | toml11, the parser mruby-toml carries, by `FetchContent` |
| liburing, slipstreamIO | gem `mruby-slipstreamio` | CMake `FetchContent` |
| miniz | submodule | CMake `FetchContent` |
| mbedded-mustache | submodule | CMake `FetchContent`, target `mustache::mustache` |
| Boost.PFR | submodule or system Boost | system Boost 1.84 or later, else `FetchContent` |
| picohttpparser | a copy in `bench/picohttpparser/` | CMake `FetchContent`, bench only |
| zlib, libcrypto, Google Benchmark, GoogleTest | system | system |

## 3. Steps

Each step is one branch, green before the next starts. Each step
builds and tests in five minutes or less, and its test run checks the
code it changes.

### 3.1 Build with CMake

- One `CMakeLists.txt` at the root. Targets: `webmachine` (the
  library), `webmachine-serve`, `webmachine-cache`,
  `webmachine-cache-check`, the bench binaries and the fuzzers.
- The dependencies of section 2 come as the table says, each pinned to
  a tag or a commit.
- CRC32C comes in as its submodule here. `src/cache.c` calls it, and its
  hand-written form and the `#error` go.
- The flags of `build_config_*.rb` become CMake presets: release and
  debug, g++ 16 with `-std=c++26 -freflection`, clang with `-std=c++26`
  and Boost.PFR.
- The debug code switches on what CMake sets for the build type, in
  place of `MRB_DEBUG` (owner): CMake defines `NDEBUG` for Release,
  RelWithDebInfo and MinSizeRel and not for Debug, so debug code stands
  where `NDEBUG` is not defined. The tree defines no switch of its own.
- `bench/*.sh`, `bench/*.cpp` and `fuzz/` read their dependencies from
  the CMake build, not from `mruby/build`. The tasks `bench`, `fuzz`
  and `crosscheck` become CMake targets.
- Check: the tools build with both presets and both compilers.
  `webmachine-serve` answers `/hello`, `/` and `/nope` byte for byte as
  before, except `Date`. A short fuzz run starts.

### 3.2 Tests in C++

- The TOML parser of 3.1 comes before this step, so the config tests
  read TOML without mruby.
- Each of the 221 `assert(` blocks becomes one GoogleTest case with the
  same name. The comment of each test says why it exists. The
  expectation comes from the specification, not from the old output.
- A finite input keeps its full check.
- Check: 221 cases, and each passes. The Ruby tests run once more on
  the old build as the reference.

### 3.3 Remove mruby

- `src/config.hpp` reads TOML through the parser of 3.1.
- Deleted: `src/webmachine.cpp`, `mrbgem.rake`, `build_config_*.rb`,
  `test/*.rb`, `test/*.hpp` of the bindings, `test/bindings.cpp`, the
  mruby parts of the `Rakefile`.
- Check: `grep -rni "mrb_\|mruby" src tools test bench fuzz Rakefile`
  finds only `src/home.hpp:43,65`.

### 3.4 The C++ API for reflection and for Boost.PFR

- A type that carries data in the API is an aggregate: public members,
  no constructor of its own, no base. Then reflection and Boost.PFR
  read the same members. Two kinds are not aggregates and are not
  read member by member: the callback interface, and an error, which
  derives from an exception class (RULES.md).
- Ownership is in the types: values, `std::unique_ptr`,
  `std::shared_ptr`. No raw owning pointer, no varargs.
- The callbacks keep the names of webmachine-ruby. A binding implements
  them.
- The entry that takes one request and runs the graph is `dispatch`, the
  name webmachine-ruby gives it. It takes no socket and no ring. Its
  response body is not one form only: the owner decided that a body
  goes straight from LMDB and that a flow can halt (question 4).
- Every header starts with a short comment that says what the file does
  and links to the specification it implements.
- Check: a probe binding with mruby-cpp-reflection calls `dispatch` from
  mruby. A probe file reads every data type with Boost.PFR under clang.

## 4. What is measured

- 3.1: the rate of `/hello` at 256 connections, the old build against
  the CMake build, same flags, alternated, after a noise floor.
- 3.4: the time of one `dispatch` without a ring, in ns, Google
  Benchmark.

No gain is claimed before its measurement.

## 5. Risks

- The bench and fuzz paths change with 3.1. Fuzzing is needed before a
  release, so 3.1 is not done until a fuzz run starts.
- `webmachine-cache` and LMDB stay native only.
- The rename to mbedded-webmachine is coming. This plan uses no new name
  in files until then.

## 6. Order

3.1, 3.2, 3.3, 3.4.

## 7. Not in this plan

- HTTP/2 and HTTP/3.
- A binding for a language other than the probe of 3.4.
- The rename of the repository.
- WASI (dropped by the owner, 2026-11-02).

## 8. Data this plan leaves on the disk

| item | mark |
|---|---|
| CMake build directories | made again by a build command |
| the old mruby build in `mruby/` | deleted after 3.3 |
| bench result files | recorded in the state file |

## 9. Open questions for the owner

1. The body of a `dispatch` response: bytes, a view into LMDB, or a body
   that comes later when the flow halts. Proposal: one response type
   whose body is one of these three.
