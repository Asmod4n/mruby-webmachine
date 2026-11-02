# Plan: webmachine as a C++ library

Status: draft, for review. Branch `plan-cpp-library`, from `integration`.

## 1. Goal

The tree holds no mruby. Everything is C++. Other languages use the
library through bindings that reflection makes from the C++ types.
There is no C API. A second target is WASI: a browser holds a full web
server, and a page reaches it through `fetch`.

Decisions of the owner that this plan takes as given (2026-11-02):

- mruby leaves the tree.
- There is no C API.
- A dependency is fetched by CMake where possible.
- WASI is built by clang, which has no reflection. There Boost.PFR 1.84
  or later reads the members.
- slipstreamIO gets a WASI mode.
- In the browser the cache is the cache of the browser.

## 2. What the tree holds today

Read on `integration` (8c76f51 and later).

| part | lines | mruby |
|---|---|---|
| `src/*.hpp`, `src/*.c`, `src/*.h` | 6667 | 2 files |
| `src/webmachine.cpp` | 16 | the gem init and final, nothing else |
| `src/config.hpp` | 119 | reads TOML through the mruby gem `mruby-toml` |
| `src/home.hpp` | 145 | the word mruby-webmachine in the page, no API |
| `tools/webmachine-{serve,cache,cache-check}/main.cpp` | - | built as gem binaries |
| `test/*.rb` | 2502 | every test is Ruby, through `test/bindings.cpp` |
| `mrbgem.rake` | - | the build |

Dependencies, and how they arrive today:

| dependency | today | after this plan |
|---|---|---|
| ada (URL) | gem `mruby-uri-parser` | CMake `FetchContent` |
| LMDB | gem `mruby-lmdb` | submodule, by the rule for LMDB |
| simdjson | gem `mruby-fast-json` | CMake `FetchContent` |
| TOML | gem `mruby-toml` | a C++ TOML parser, by `FetchContent` (to be chosen, see 9) |
| liburing, slipstreamIO | gem `mruby-slipstreamio` | CMake `FetchContent` |
| miniz | submodule `deps/miniz` | CMake `FetchContent` |
| mbedded-mustache | submodule | CMake `FetchContent`, target `mustache::mustache` |
| Boost.PFR | submodule or system Boost | system Boost 1.84 or later, else `FetchContent` |
| zlib, libcrypto | system | system |
| Google Benchmark | system | system |

## 3. Steps

Each step is one branch, green before the next starts.

### 3.1 Build with CMake

- One `CMakeLists.txt` at the root. Targets: `webmachine` (the
  library), `webmachine-serve`, `webmachine-cache`,
  `webmachine-cache-check`.
- Every dependency of the table above that is not a system package
  comes through `FetchContent`, pinned to a tag or a commit.
- The flags of `build_config_release.rb` and `build_config_debug.rb`
  move into CMake presets: release and debug, g++ 16 with
  `-std=c++26 -freflection`, clang with `-std=c++26` and Boost.PFR.
- `MRB_DEBUG` becomes a definition of the debug preset with a name of
  this tree's own (to be named, see 9).
- Check: the three tools build with both presets and with both
  compilers. `webmachine-serve` answers `/hello`, `/` and `/nope` as
  before, byte for byte except `Date`.

### 3.2 Tests in C++

- The 2502 lines of Ruby tests become C++ tests. Framework: GoogleTest,
  as mbedded-mustache uses it.
- Each `assert` becomes one test case with the same name. The comment
  of each test says why it exists, as the rules ask.
- A finite input keeps its full check (every byte value for a byte
  set).
- Check: the count of cases is equal to the count of `assert` in the
  Ruby files, and every case passes. The Ruby tests run once more on
  the old build as the reference.

### 3.3 Remove mruby

- `src/config.hpp` reads TOML through the parser of 3.1.
- `src/webmachine.cpp`, `mrbgem.rake`, `build_config_*.rb`,
  `test/*.rb` and `test/bindings.cpp` are deleted.
- The `Rakefile` keeps only what is not about the build, or is deleted.
- Check: `grep -rn "mrb_\|mruby" src tools test` finds nothing.

### 3.4 The C++ API for reflection and for Boost.PFR

- Every type that the API shows is an aggregate: public members, no
  constructor of its own, no base. Then reflection and Boost.PFR read
  the same members.
- Ownership is in the types: values, `std::unique_ptr`,
  `std::shared_ptr`. No raw owning pointer, no varargs.
- The callbacks of webmachine stay a C++ interface with the names of
  webmachine-ruby. A binding implements it.
- One function answers one request: it takes a request as a value and
  gives back a response as a value, with no socket and no ring. The
  server and the browser both call it.
- Check: one probe binding, written with mruby-cpp-reflection, calls
  the API from mruby. A probe file reads every public type with
  Boost.PFR under clang.

### 3.5 WASI

- Target `wasm32-wasip1` with clang 23, Boost.PFR in place of
  reflection.
- The entry of 3.4 is the export that a service worker calls on a
  `fetch` event: request in, response out.
- No ring, no socket, no LMDB, no thread under WASI. The cache is the
  cache of the browser; the tree gives correct `ETag`,
  `Last-Modified`, `Cache-Control` and `Vary`.
- Files come from the file system of the WASI shim, through
  slipstreamIO in its WASI mode.
- Exceptions: `-fwasm-exceptions`. Which shim and which browser carry
  them is measured, not assumed.
- Check: a page with a service worker loads the module. `fetch('/')`
  gives the same body as the native server.

### 3.6 slipstreamIO in WASI mode

- A separate plan in slipstreamIO. This plan needs from it: files,
  timers, and the ring API with no thread.

## 4. What is measured

- 3.1: the rate of `/hello` at 256 connections, the old build against
  the CMake build, same flags, alternated, after a noise floor.
- 3.4: the cost of the entry of one request without a ring, in ns,
  Google Benchmark.
- 3.5: the time of one `fetch` through the service worker in one
  browser. A number only after the measurement.

No gain is claimed before its measurement.

## 5. Risks

- The TOML parser: the choice of a library is the owner's (see 9).
- The Ruby tests read some values through mruby objects. Their
  expectations are taken from the specification, not from the old
  output.
- `webmachine-cache` and LMDB stay native only. The browser build has
  no store.
- The rename to mbedded-webmachine is coming. This plan uses no new
  name in files until then.

## 6. Order

3.1, 3.2, 3.3, 3.4, then 3.5 and 3.6 side by side.

## 7. Not in this plan

- HTTP/2 and HTTP/3.
- A binding for a language other than the probe of 3.4.
- The rename of the repository.

## 8. Data this plan leaves on the disk

| item | mark |
|---|---|
| CMake build directories | made again by a build command |
| the old mruby build in `mruby/` | deleted after 3.3, made again by nothing |
| bench result files | recorded in the state file |
| the browser probe page | needed by open work until 3.5 is done |

## 9. Open questions for the owner

1. TOML parser: toml++ (header only, C++17, the most used) or another.
   Proposal: toml++.
2. Name of the debug switch that replaces `MRB_DEBUG`. Proposal:
   `WEBMACHINE_DEBUG`, renamed with the repository.
3. WASI version: `wasip1` (every shim carries it) or `wasip2`.
   Proposal: `wasip1`, because the browser path needs no socket.
