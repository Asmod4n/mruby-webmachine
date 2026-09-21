# Where the work stands

## The trees

- `mruby-webmachine` is the tree that is built. Branch `dev`, remote
  `Asmod4n/mruby-webmachine`.
- `webmachine-archive` is read only. Read it for prior art. Never
  write in it.
- `webmachine-mruby` is closed. The owner said so. Do not read it, do
  not write in it, do not quote it.

## What one command does

    rake test

It builds the debug config and runs every test. Nothing in that run may
fail. `rake -T` lists the other tasks and the environment variables they
read. `bench/instructions.sh --help` prints its own.

## What is committed

Four commits on `dev`:

    887b1e3  Write RFC 9110 sections 12.5.5, 13.2.2, 14.3 and 14.4
    b7f01ec  Let mrbtest count what a constant expression answers
    3002ff9  Keep comments in test/ alone
    ca58d24  Take the .clang-format of the archive

Last green run: `rake test` 1815 checks, 0 failed, 0 crashed.
`rake crosscheck` walked 3368 corpus inputs under qemu and the five
tables of the NEON scanner agreed with the byte loop. `rake fuzz[60]`
ran 5,759,422 inputs and broke no oracle.

## RFC 9110 in src/http.hpp

Written: 4, 5.6.1 to 5.6.4, 5.6.6, 5.6.7, 6.6.1, 8.3 to 8.8, 9, 10.1.1,
10.2.1, 10.2.3, 11.1, 11.3, 11.4, 12.4, 12.5.1, 12.5.3 to 12.5.5, 13,
14.1 to 14.4, 15. Beside them: RFC 9112 3.2, RFC 3986, RFC 9651 and
RFC 10008.

Skipped, by decision: 5.6.5 comments, 6.5 trailers, 12.5.2
Accept-Charset.

## Decisions that stand

- No state is cached. The cache holds bytes that are the same for every
  visitor, so a cookie is neither a cache axis nor a route name.
- No virtual hosts. `proto://server` is one `std::string` built at boot.
  Host awareness is needed for SNI alone, which sits below HTTP.
- The flow always runs. A konst resource folds it at compile time, so
  `flow::answer` reads `shortcut.status` instead of walking the graph.
  No Ruby runs per request for such a resource.
- `static_assert` stays where nothing else can find the bug. A binding
  writes `constexpr bool answered = f();` and the `.rb` file reports
  `answered`.
- No comments outside `test/`. A tool says what it does through
  `--help`, or through `rake -T`.
- A commit message carries no link to a tool, and no address of the
  owner.

## Numbers, and where each one was taken

- The browser head costs 8.5 percent of the rate on `forgecore`, which
  is a Zen 3 Ryzen 9 on openSUSE: 875,730 against 956,819 responses a
  second, one session, htgen at `-c192`. Those rows do not alternate and
  the client held 80 percent of a core, under the floor of 85, so the
  figure is the archive's own evidence and not a measurement made to the
  rules.
- The same pair on this container reads 15.0 percent: 467,704 against
  549,927, five rounds, alternating. The machine is sapphirerapids at
  2100 MHz under kvm inside docker.
- A number from one machine is never set beside a number from another.
  Read `bench/README.md` and every `.sh` under `bench/` before making
  any claim about speed.

## What is open

1. `RULES.md:1101` says a route grows to name "a query parameter, a
   cookie and a field". The owner has since decided that a cookie
   cannot be a route name. Proposed replacement for the sentence: "A
   route names path segments today, and it grows to name a query
   parameter and a field." The owner has not answered.
2. `bench/README.md` is prose outside `test/`. It states why and it
   compares. The owner has not said whether it stays, is rewritten, or
   goes.
3. `spell_retry_after` has two overloads, one for a delay and one for a
   date. Rule 1 asks that a declaration say what happens, and the
   overload does not say which form comes out. A split into
   `spell_retry_after_delay` and `spell_retry_after_date` was proposed
   and not answered.
4. The route grammar is settled in discussion and unwritten: this
   tree's own grammar, braces in the OpenAPI shape, origin-form with a
   query, three constructs that compile to `kLiteral`, `kBinding` and
   `kSplat`. It waits on the dispatcher.
5. `CLAUDE.md` in the closed tree carries an edit to rule 13 that was
   never committed. It is the owner's to keep or drop. Do not go there.

## How to test a change to the wide scanner

`allowed_run_length` compiles one of three arms. A build here takes
AVX2, so the NEON arm is reached only by `rake crosscheck`, which cross
compiles and runs it under qemu. That answers correctness alone: qemu
models no pipeline, so no row of `bench/results` is ever made that way.
