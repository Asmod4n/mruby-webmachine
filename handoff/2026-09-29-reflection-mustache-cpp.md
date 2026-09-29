# Handoff: mustache-cpp, mruby-mustache, mruby-cpp-reflection

Read RULES.md, .claude/RULES.md and the files of claude-state first:
`CLAUDE.md`, `RULES.md`, `.claude/RULES.md`, `mruby-mustache.md` and
`mruby-cpp-reflection.md`. Every decision below is written there with
its reason. This file says where the work stands.

## What happened

The container was reset on 2026-09-29. Everything that was only local
is gone: the scratchpad, the podman images, g++-16, and every commit
that was not pushed. GitHub answered 503 after the reset, so nothing
could be cloned again.

## What is on GitHub

- `Asmod4n/claude-state` main at 16b5cc6: all decisions of this work.
- `Asmod4n/mruby-cpp-reflection` branch `borrowed-and-heredocs` at
  353c877: the word `borrowed` with `.owner`, the c:/cxx: texts, the
  template rule, and the fix of the ASan report of `fails_in_child`.
- `Asmod4n/mruby-mustache`, pushed branches in this order:
  `template-holds-symbol-keys` (6496b6b), `render-writes-through-host`
  (c3a869c), `values-without-copy` (2d5282b),
  `reflect-writes-through-host` (44d85a2), `render-into-growing-buffer`
  (cd60200). None is merged into main.

## What is lost and has to be made again

1. **mustache-cpp.** The C++ core of mruby-mustache as its own library.
   The owner wants it on a branch of mruby-mustache for now. Rebuild it
   from `render-into-growing-buffer`:
   - `include/mustache/` with its history (git filter-repo), CMake with
     an INTERFACE target, C++ tests with the spec cases.
   - The 8 `static_assert` in `reflect.hpp` become deleted functions
     with a reason: `nests_too_deep`, `does_not_compile`,
     `names_a_cxx_keyword`, `names_no_member`, `is_not_text`. Names
     approved. The two `// namespace` comments in `compile.hpp` go.
   - Branch form A everywhere: an early exit with `[[unlikely]]`.
     `[[likely]]` only with a measurement.
   - `std_host` keeps its data map as `std::unordered_map` with the
     precomputed hash.
   - The render writes directly into a fresh string of the host
     language, with no buffer of ours between.
   - The C API, `include/mustache-c/mustache.h`, prefix `mustache_`:
     every function returns 0, or -1 with errno set. The header has no
     struct. Callbacks are set one by one on the template:
     `mustache_set_find`, `_kind`, `_text`, `_size`, `_element`,
     `_new_string`, `_grow`, `_done`. Every callback returns a pointer
     or a scalar. The real capacity comes back through `size_t *`.
     `grow` takes `void **string`, so a host can replace its handle.
     errno: no_memory ENOMEM, over_limit E2BIG, too_deep ELOOP,
     over_work ETIME, not_text EINVAL, parse EILSEQ. The message of a
     failure comes from `mustache_message`. Names approved.
   - The root comes with a release function and is held in a
     `std::shared_ptr`. Its control block comes from a pool of the
     template.
2. **mruby-cpp-reflection:** the declaration for a pointer field,
   `borrowed(^^ImGuiIO::Fonts, {.owner = ^^ImGuiIO})`, Rake form
   `borrowed 'X::f', owner: 'X'`. The wrapper reads the pointer from
   the field at each access, and a null field gives nil. With it,
   Dear ImGui rendered a frame from Ruby: 1 draw list, 2 commands, 126
   vertices, 213 indices.

## What is next

1. Push mustache-cpp as a branch at once, so that a reset cannot take
   it again.
2. Measure the errno C API against the form with a struct of
   callbacks: simple, stocks, news, with reflection as reference. A
   reminder was set for 2026-09-29 17:43 UTC.
3. Reflection: rebuild the pointer field declaration and the ImGui
   frame probe. Then nlohmann/json, protobuf, llama.cpp and OpenCV,
   each as its own probe gem with only an `mrbgem.rake`. Then a
   window with a GtkNotebook, and webview/webview inside it as a tab,
   from Ruby, under xvfb-run and ASan. `ascaridol/tools/ascaridol/
   ascaridol.cc` shows how the native window, the GTK main loop and
   the webview handles are used.
4. Then a security review with Fable.
5. Later: mruby leaves the reflection gem. The core becomes a C++
   library with CMake, header-only where it can be, and a C API that
   reads a description table made at compile time.

## Rules the owner added in this work

- A build is rare. Up to four builds run at the same time, each for a
  different problem.
- The owner reads on a phone: an answer has at most ten lines per
  block, and the numbers come first.
- mruby is not measured for mustache; the two C++ modes are
  (reflection and runtime).

## Open questions

- Nothing open that the owner has not answered. The measurement of
  item 2 waits for the owner's go.
