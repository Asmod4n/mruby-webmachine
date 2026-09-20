# Rules

## Write in Simplified Technical English

Everything written in this repository follows ASD-STE100 -
https://asd-ste100.org/ - and everything means everything: names,
commit messages, documentation, help text and error messages.

- One thought per sentence.
- Short sentences. About 20 words is the limit.
- Active voice. Name who does the thing.
- Simple words. One word keeps one meaning.
- No metaphors, no idioms, no rhetorical questions.
- No word in capitals for emphasis. An acronym and the name of a
  constant are not emphasis.

A name follows the same rules. A function is named for what it does. A
file is named for what is in it.

## Everything is in English

File contents, documentation and commit messages.

## A declaration says what happens

The function name says what the function does. Each parameter name says
what is passed. The return type says what comes back. A reader of the
header alone knows the behaviour.

## A name that a specification gives is the name the code uses

A field, a method, a status, a frame, an opcode, a close code, a rule
of a grammar: the word of the specification, without translation.
`tchar`, `token`, `OWS`, `quoted-string`, `field-name`,
`representation`, `origin`.

Where the C++ standard names the thing instead, the standard wins for
the same reason: `what()` stays `what()`.

Where nothing names it, the name says its purpose.

A specification names the thing, not the work. So a function is named
with a verb and then the word the specification gives the thing:
`parse_quoted_string`. A predicate already reads as a verb and keeps
the plain form: `is_tchar`.

The order of a class follows the order of the specification that
defines it, section by section.

## No method hides anything

No function wraps one library call. No function changes an argument
unless its name says so. No function raises unless its name says so.
No function does two things.

## Every argument is const

A function reads its inputs and returns its result. There is no output
parameter and no pointer that is written through. The return type is
what the function makes. A function that changes an object is a method
of that object.

## Nothing is copied to pass it

A type that owns memory is passed by `const&`: `std::string`,
`std::vector`, a class with a buffer inside.

A view and a scalar are passed by value, because that is already the
form that does not copy. A `std::string_view` is a pointer and a
length. It points at the bytes and does not hold them. A reference in
front of it adds one indirection and takes nothing away.

A function that needs its own copy says so by taking the value, and
then the caller can move into it.

## A type has no constructor unless it needs one

A type is an aggregate: public members, no constructor of its own, no
base, nothing virtual. Then it works in a `constexpr` context, the
compiler can copy it with a move of bytes, and there is no hidden work
at its creation.

A constructor is written where the type cannot hold a wrong value
otherwise, or where what the type is demands one. Where that
constructor also has to allocate, it allocates.

An error is the second case. An error derives from an exception class,
in C++ as in Ruby, because both languages decide at the type what a
`catch` and a `rescue` find. A class that carries the methods of an
exception and does not derive is not one, and no handler that looks
for an error will see it. `std::runtime_error` holds a string and has
a vtable, so an error is not an aggregate and not `constexpr`. That is
what an error is, not a price it pays.

## Use what a library already has

The standard library and every linked library come first. Nothing they
answer is written here a second time. Read the library in its own
source before you say it cannot do the thing.

Never do pointer arithmetic by hand. Work in views and indices:
`std::string_view` or `std::span` for the run, `std::distance` for the
offset, `std::next` to walk it.

The small ones count as well: `std::min` and `std::max` rather than a
ternary, `std::from_chars` rather than a digit loop, `std::clamp`
rather than two ternaries.

What stays ours is what no library answers: a rule a specification
states.

## An index is checked by the compiler

`std::array` and `std::vector` are read and written with `.at()`,
never with `[]`.

In a `constexpr` context an index outside the bounds becomes a
compiler error, because a throw is not a constant expression. At run
time it costs nothing where the type already proves the bound: gcc
emits the same four instructions for
`table.at(static_cast<unsigned char>(c))` and for
`table[static_cast<unsigned char>(c)]`, measured with `objdump` on
`-O3 -march=x86-64-v3`.

## Kernighan and Ritchie

`.clang-format` states the layout: `BreakBeforeBraces: Linux`, four
spaces, the star on the name.

A name is short where its scope is short and says its purpose where
its scope is long. The C++ Core Guidelines state this as NL.7.

## An error goes to whoever can repair it

A fault of ours is raised. An impossible state, a violated
precondition, an index that the thrower packed wrong. With a VM in
hand it is `mrb_raisef` with the right error class. Without a VM it is
a throw, from `std::logic_error` or a child of it. Nothing catches it.
The process ends.

A configuration the server cannot read goes three ways, and which one
depends on who is there to hear it.

At the start there is no configuration to keep. The reader gives the
error back as a value, `main` writes it and the server does not start.

At a reload from the file nobody is standing there. The error goes to
the error log, the server keeps the configuration it already has, and
it serves on. A reload builds the whole new configuration as a value
first and swaps it in one step, so a failure in the middle leaves the
old one whole.

At a call from the application there is a caller, and the caller has a
VM. It is `mrb_raisef`, so the author gets a class, a message and a
backtrace into their own line.

Everything else is a value and the server keeps serving. A client that
sends something invalid, a syscall that failed, a file that is gone.
They come back in `std::expected`.

A value of that kind is still an exception class. It derives from
`std::runtime_error` for a condition of the world and from
`std::system_error` for a syscall, with the errno in its `error_code`.
The type says what the thing is. It does not say how it travels.

Two things arrive thrown that are not ours. mruby throws an
`mrb_jmpbuf *` at every raise, and `mrb_protect_error` turns it into a
value. The standard library and the dependencies throw their own. A
catch names the exact type it can recover from.

`main` and the body of a thread we start catch what reaches them,
because the alternative is a death with no words. Both write or record
it and then end. Neither continues.

## Comments live in tests, and they say why

`src/` carries no comments. Not a note, not a section number, not a
name of a specification. What the code does, the code says. A reader
who needs more than the declaration has found a name that is wrong,
and the answer is the better name.

A test carries comments, and they answer one question: why this test
exists. A section of a specification, an issue, a pull request, a CVE.
Where the rule is a grammar that a reader does not know by heart, the
comment says in plain words what the rule allows. What the test does
is in the test.

    # RFC 9110 5.6.2 Tokens
    # A token is a name without quotes: a header field name, a method,
    # a parameter name. It holds letters, digits, and these 15 marks:
    #     ! # $ % & ' * + - . ^ _ ` | ~
    # Nothing else. No space, no tab, no colon, no bracket, no quote.

A string that reaches a log, an error page or a client is not a
comment. `"RFC 9110 5.6.4"` inside an error record is what the
operator reads at three in the morning. It stays.

## The compiler vectorizes, and we check that it did

A comparison over a run of bytes is written in the form both compilers
turn into vector instructions: a loop with a count known before it
starts, over a contiguous buffer, with no early exit and no branch in
the body. `std::ranges::all_of` and `std::ranges::find_if_not` over a
`std::string_view` are that form.

Then we read what the compiler did, rather than assume it:

    gcc    -fopt-info-vec -fopt-info-vec-missed
    clang  -Rpass=loop-vectorize -Rpass-missed=loop-vectorize

Both compilers, and both architectures: x86-64 and aarch64. A loop
that one of the four does not vectorize is rewritten until it does.

An intrinsic is written only where all four say they cannot, and only
where an instruction count is lower for the hand written form, both
arms built with the same `-march=`. Then it is written for AVX2 and
for NEON at once, with the plain loop as the third branch. AVX2 and
NEON are the floor of what such code may use.

## Measure before, measure after

A number about speed comes from a measurement in this session, on this
machine, with both arms built the same way.

Measure the noise floor of the machine first. Ten runs of
`sysbench cpu` say how small a difference the clock can still read
there. A difference under that floor is not a difference.

The arms alternate, A B A B A B, five runs each, and the medians are
compared. A change too small for the clock is counted with valgrind,
because the count of one binary does not move between runs.

A number with no measurement behind it is not written down.

## Asserts live in tests

No `assert` and no `static_assert` in `src/`, and none in a test
`.cpp` either. mrbtest counts what `assert` in a `.rb` file reports,
and a check that mrbtest does not count is a check nobody sees.

One `.cpp` in `test/` holds every binding the `.rb` files need, and
one `.hpp` beside it for each module under test. The `.cpp` includes
them.

Where the input of a function is a finite set, every member of the set
is checked. A predicate over a byte has 256 inputs, so all 256 are
checked. The expectation is written from the specification and not
from the table under test, so a wrong table has something to disagree
with.

## A clone is recursive

`git clone --recursive`. A repository that has no submodule today can
have one tomorrow, and a clone without them fails later and somewhere
else.

## Make the work easy for the compiler

The compiler produces the fast code. Our part is to leave it nothing
to be careful about.

**Let it see everything.** A function the compiler cannot see, it
cannot inline. The parsing code is in headers for that reason, and
what is not gets `-flto`.

**Nothing is allocated on the path that succeeds.** An allocation is a
call the compiler cannot look into, and it ends every assumption it
had about memory.

**A result is read as soon as it is made.** Six results gathered into a
list, only to find the first failure afterwards, read all six fields
before anything is wrong and copy six objects that are not trivial.
Write the six tests one after the other instead. Each one ends the
function where the fault is.

**No exception is thrown on that path either.** An error is a value,
so the straight line through a function has no unwinding in it.

**An indirect call stops inlining.** A virtual method, a
`std::function`, a pointer to a function: the compiler has to assume
the worst about all of them. A table of values is not an indirect
call. A table of function pointers is.

**A fixed size, checked once, at the top.** After `text.size() != 29`
the compiler knows the length of every `substr` below it and drops the
tests. A check in the middle of the work cannot do that.

**Do not take the address of a local.** A pointer that leaves the
function forces the value into memory. Views and indices keep it in a
register.

**Then measure, and believe the measurement.** The compiler is right
more often than the person rewriting the loop. A change that is only
believed to be faster does not enter the tree.

## A parser sees the whole field value at once

Every function that reads a grammar takes one `std::string_view` and
reads it to the end. None of them holds a state between calls, and
none of them can stop in the middle and go on later.

This works because of what stands in front of them. picohttpparser
reports a header only when the whole field is in the buffer, and HPACK
gives back whole fields as well. So a field value is contiguous in
memory by the time a parser sees it.

That is a condition, not a fact of HTTP. A server that reads bytes and
parses them as they arrive needs a state machine that can stop between
two bytes, which is what llhttp and Boost.Beast are. If the layer in
front of these parsers ever changes so that a field value can arrive in
two pieces, this shape is wrong, and the answer is a state machine and
not a patch.

## Each part does one thing, and the others do not know how

A module owns the types of its dependency and shows them to nobody.
The HTTP/1.1 part owns picohttpparser, the HTTP/2 part owns ls-hpack,
the compression part owns zlib. A type from a dependency does not
appear in the declaration of another part.

The reason is that one part must change inside without a second part
changing with it.

## One HTTP, and the versions under it

`Http` holds the semantics of RFC 9110 and RFC 9111: request,
response, fields, representation, URI, and the rules that read them.
`Http1`, `Http2` and a later `Http3` turn those values into bytes and
back, and add nothing of their own. The decision graph reads a request
and makes a response, and it cannot see which version carried them.

An upgrade is not a state of HTTP. It is the end of it: the resource
takes the connection and speaks something else, and the graph never
runs. That belongs to the version, not to `Http`.

## Every function could run in a functional language as it is

A function takes values and returns a value. It reads no global. It
writes no global. It keeps no static. State that changes is a value
that goes in and a new value that comes out.

A clock is a value too. A function that needs the current year takes
it as an argument.

An effect happens in one place, after a pure function decided it. An
effect is a system call, a ring submission or a call into Ruby.

## A class is one defined kind of work

What a specification defines as one kind of work is one class, and the
specification names it. RFC 9110 gives `Http`, RFC 9112 gives `Http1`,
RFC 7541 gives HPACK. The webmachine decision graph gives the classes
of webmachine, with the names webmachine-ruby uses.

Work that no specification defines waits until it has one.

## A name claims nothing that does not happen

There is no zero copy here, so nothing is named for it. A name that
says `zero_copy` where the kernel still copies teaches the next reader
something false.

A name says what happens and, where it is not plain, why. A threshold
above which the body is sent from its own memory instead of a copy is
`send_body_without_copy_above`.

## A rule states what holds

A rule does not say how often it is broken, who breaks it, or what
most people do. Those are claims, and a rule carries none. Where a
rule has a reason, the reason is one that a reader can check.

## conf.url is an ask, and it reads back as an answer

`conf.url = "http://0.0.0.0:0"` is what the operator wants. What the
kernel bound is another thing, and it is the thing an application needs.
So the server binds, reads the name back with `getsockname`, spells a
URL out of what it read, and writes that into `conf.url`. Then
`app.ready` runs, and the block reads a URL that is true:

    app.ready do
      puts app.conf.url        # http://0.0.0.0:39241, the kernel's port
    end

Every part of that URL comes from the answer and not from the ask. The
port, because port 0 is the case that makes this necessary and a test
that wants to connect needs the number. The host, because `0.0.0.0` and
`::` are addresses an operator did not type. The scheme, because a
listener with TLS is `https` whatever the configuration said. A unix
listener reads back `unix://` and its path.

The archive had half of this and the half was wrong. It read the port
back from the kernel only where the ask was port 0, and it built the
string from what the operator had typed, with `"http://"` in front of
it - so an https listener read back an http URL, and nothing in the
suite asked. A value that is right in one of two cases is worse than one
that is always computed: the reader cannot tell which case they are in.

`getsockname` comes through the ring
(`io_uring_prep_cmd_getsockname`). A kernel that cannot do it is a
kernel this server does not run on, and the refusal says so at boot.

`conf.url = "http://0.0.0.0:0"` is a valid ask and the one this exists
for. What ada makes of the forms an operator writes was measured
against the vendored copy, and one row of it is a trap:

| written | `get_protocol` | `get_hostname` | `get_port` | `get_pathname` |
|---|---|---|---|---|
| `http://0.0.0.0:0` | `http:` | `0.0.0.0` | `0` | `/` |
| `http://0.0.0.0:80` | `http:` | `0.0.0.0` | *empty* | `/` |
| `https://0.0.0.0:443` | `https:` | `0.0.0.0` | *empty* | `/` |
| `http://0.0.0.0` | `http:` | `0.0.0.0` | *empty* | `/` |
| `http://[::]:0` | `http:` | `[::]` | `0` | `/` |
| `unix:///run/a.sock` | `unix:` | *empty* | *empty* | `/run/a.sock` |
| `unix://relative.sock` | `unix:` | `relative.sock` | *empty* | *empty* |
| `http://0.0.0.0:65536` | refused by ada | | | |

An empty port is not port 0. ada drops the port that is the scheme's
default and keeps every other, so an empty `get_port()` means 80 under
http and 443 under https, and `"0"` means the kernel chooses. Code that
reads the empty string as a zero binds an ephemeral port where the
operator wrote 80.

Two more the table states. `:080` and `:00` come back normalized, so
the digits are ada's to check and the range as well - 65536 never
arrives here. And a `unix://` URL with no third slash makes a host and
no path, which names no socket and is refused with the path it did not
have.

## A gem that only the tests need is a test dependency

`spec.add_test_dependency` in `mrbgem.rake`, not `conf.gem` in a build
configuration. A gem in the configuration is in every build and in the
library that ships.

## A grammar is read, not remembered

`refs/` holds the specifications this tree implements, verbatim from the
RFC Editor. A claim about a rule is checked there before it is written,
and a session with no network can still check it.

    grep -n "absolute-path = " refs/rfc9110.txt

A specification this tree starts to implement is downloaded in the same
commit as the first function that reads it.

## mruby has the methods CRuby has

Every Array, Hash, String, Enumerable, Numeric, Symbol and Kernel method
of CRuby is in mruby. They live in the `-ext` gems, and a gem that is not
named in `mrbgem.rake` is not in the build.

So `undefined method 'sum' for Array` does not mean mruby has no `sum`.
`Enumerable#sum` is in `mruby-enum-ext`, and that gem was not named. The
message reads exactly like a missing method and it is a missing line in
`mrbgem.rake`.

Never write around it. A hand rolled `inject` in place of `sum`, or a
loop in place of `each_with_index`, hides a build that is short a gem and
leaves the next test to find the same wall.

Every `-ext` gem is a test dependency here, so a test is written in plain
Ruby.

## No bang methods

No method with `!`. A question a caller may ask is a `?` predicate.

## A number about speed comes from Google Benchmark

No hand written loop with a clock around it. Google Benchmark decides
how many iterations a case needs, repeats it, and reports the median
and the deviation, so a number arrives with the spread that made it.

A measurement compares before against after in **one binary**, with both
arms built the same way and run alternately. There is no other valid
comparison here.

So the old implementation stays, as an arm of the benchmark, until the
change it is being judged against is decided. Deleting it first and
reading the next run against the last one measures the link.

This rule exists because a hand written harness gave 29, 45, 63 and 74
nanoseconds for one unchanged function, and 43 against 64 in two runs
of one binary. sysbench read 2.5 percent of spread over runs of a fifth
of a second, so the machine was not the reason. The harness was.

Whether two cases in one process disturb each other is a question for
the first measurement, not an assumption.

A number is tied to the binary that produced it, and a relink is a new
binary. The same source, one machine, alternating at raised priority
and a load of 0.09, read 66 ns against one build of the timing library
and 90 against two others - and with `-falign-functions=64
-falign-loops=64 -falign-jumps=64` all three read 89.0 plus or minus
0.5. Nothing about the library explained it. Linking something else
moved the hot loop, and where a loop sits is worth a third of its cost.

So: the alignment flags are always on, two arms are compared inside one
binary, and an absolute nanosecond figure is never carried from one
build to the next. Below roughly a third, the clock cannot answer a
question across binaries at all, and `bench/instructions.sh` is what
can - it counts what the binary executed, and that count does not move.

Measured the same day, and the reason the rule is written this hard:
one change read 31.3 ns before and 42.1 after, which looks like a
regression of a third. The two implementations in one binary read 3.08
and 2.32, which is an improvement. The 42.1 was a different link.

This was learned the long way. A relink changed a median by 30 percent,
it was called code layout, then an instruction count disagreed and the
layout reading was dropped, and four wrong mechanisms were proposed -
a debug timing library, a version bump, AVX-512 licence throttling and
the machine's own load - before the flags settled it. Each one was
plausible and each one was stated before it was tested.

The library is the machine's. It was vendored for a while, to pin a
version and to match the harness's instruction set, and the measurement
above took that reason away: with the alignment flags on, three builds
of it answer the same. A dependency kept for a reason that has been
disproved is a dependency to remove.

What matters is that a row says which library made it. Debian's carries
no `NDEBUG` and warns about that on stderr, where a harness reading only
stdout never saw it; Google Benchmark also writes `library_build_type`
into the context, so the row carries the warning even when nobody reads
the terminal. `WM_MARCH=` still pins the harness's own ISA, because
`-march=native` is not one instruction set across two containers.

Nothing this tree runs sends stderr to /dev/null. That is how the
warning stayed unread for a day.

Every row records `benchmark_lib`, `ran_as`, `bench_nice` and
`bench_threads_max`, and Google Benchmark adds `library_build_type`
itself.

## The bench takes the machine, and says how much of it

One cpu fewer than the box has, for the whole measurement, the server
and the client together, so one of them stays out of it.

The run goes to nice -15 and everything else this user owns goes to
+15, skipping the run's own ancestors. Thirty points; ten does not do
it, and +19 alone cannot build the gap from above. No sudo: a negative
nice needs `RLIMIT_NICE`, granted once to a user in
`/etc/security/limits.d`, and without the grant this is a no-op rather
than a refusal. The row records whether it got it.

On an idle machine this buys nothing. It exists for a machine that is
not idle, and the thing that is not idle here is us: an agent building
and testing in the background moves one arm of a comparison and not the
others. Measured today - a `cmake --parallel` left the one minute load
at 1.86 while a benchmark ran.

And the run is never root. Root holds CAP_SYS_NICE and CAP_IPC_LOCK, so
`RLIMIT_NICE` and `RLIMIT_MEMLOCK` are advisory for it, and io_uring
charges its SQ and CQ rings against memlock: on 6.18 with an 8192 KiB
limit, root opened 512 rings of 32768 entries without a refusal where an
unprivileged user was stopped at two. A provided buffer pool is ordinary
memory and is not charged. Where no `bench` user exists the run goes
ahead as the caller, and the row says which it was.

## An error is small where it travels and full where it is read

A function gives back what its caller can use, and nothing more.

- A helper that can fail in one way gives `std::optional`. It needs no
  payload: the caller knows which helper it called, so the caller names
  the rule and the offset.
- Everything else inside the tree gives `std::expected<T, Refusal>`.
  A `Refusal` is the number of the problem and the offset, eight bytes,
  in a register. That is enough for a log line.
- `ParseError`, with the section, the rule, the allowed bytes and an
  excerpt, is built once, at the outside, where a user calls in: the
  server's own binary, or somebody who uses this as a library.

We are not a conformance tool. A server refuses and serves on, so the
detail a refusal carries has to be cheap enough to carry always. Eight
bytes are. Sixty four are not.

Measured on the timestamp parser: 47.0 ns with a full record in every
return, 28.5 ns with helpers on `std::optional` and a `Refusal` above
them. The same full record on `parse_field_value_parameter`, which runs
once per parameter, cost 16.3 ns to 31.6 ns.

That number says what an error carries, and it says nothing about the
wrapper. The wrapper was measured after it: `parse_host` in three
shapes, in one binary, `WM_MARCH=x86-64-v3`.

| shape | a name | a name and a port | a refused name |
|---|---|---|---|
| `std::expected<Host, Refusal>` | 10.1 ns | 15.5 ns | 8.04 ns |
| `std::optional<Host>` | 10.0 ns | 15.5 ns | 8.05 ns |
| a `Refusal` inside the returned value | 6.96 ns | 15.5 ns | 8.08 ns |

Medians of five repetitions, all three arms in one binary.
`std::expected` costs nothing over `std::optional`: both are 32 bytes
here and both come back in registers. The third shape wins 3 ns on the
short name alone, where the work is small enough for one branch to
show, and it loses the thing the other two have: the compiler makes
nobody look. So `std::expected` stays the shape of a function that can
refuse, and "it is slow" was never measured about it.

One call of it is banned inside the tree: `.value()`. It throws
`std::bad_expected_access<Refusal>`, and no catch here names that type,
so it ends the process. Ask with `if (!got)` and read with `*got`.
simdjson has the same pair and calls them the same way.

## The optimal case is optimized, and the cold path stays usable

The straight line through a function is the one that succeeds, and it
is the one that is made fast. Two limits hold that in check.

A cold path does not become pathologically slower for it. Somebody who
sends a stream of invalid requests must not cost more than somebody who
sends valid ones would.

An error reaches the end of its function at once. Every test returns
where the fault is found, and no test waits for five more to be read.

## Every error branch carries [[unlikely]]

`if (...) [[unlikely]] return std::unexpected(...)`. The compiler lays
the cold block out of the way and stops optimizing it for speed.
Measured: valid input 10 percent faster, invalid input 6 percent
slower.

## An error object does not allocate

`std::runtime_error` holds a string, and a string built from a literal
is one allocation for every refused request. That is a lever an
attacker pulls. The base takes an empty string, which libstdc++ answers
with a shared representation and no allocation, and `what()` gives the
title out of the table of problems. Measured: the error path went 32
percent faster.

## A wide read needs a page behind it, not a promise from the caller

A function that classifies bytes reads 32 at a time and masks what lies
behind the run it was given. Those bytes have to exist.

The first answer here was a `readable_bytes` parameter: the caller
counts how far the read may go. That answer is wrong, and ASan says so.
With a count that is 4096 too large the wide scan is a
`heap-buffer-overflow`, `READ of size 32`, zero bytes past a 40 byte
region. The scalar form cannot be made to do that. So the parameter
moved a memory safety obligation onto the caller through a bare
`size_t`, where the compiler checks nothing and a wrong answer is a
crash under load.

Three rules replace it, and which one holds depends on who mapped the
memory.

**Memory this process maps.** One spare buffer at the end of the
mapping, never handed out. Then every byte in the pool has
`kWidePadding` readable bytes behind it, and no wide reader asks a
question. The ring maps `(kBufCount + 1) * kBufSize` and registers
`kBufCount`.

**Nothing else is parsed today.** The bytes a parser reads come from the
pool and from nowhere else. An mruby string is a response body on its
way to liburing, not something a grammar is read out of.

**What crosses a thread is allocated with the padding.** A compute
worker and a watcher each need their own copy, and that copy is an mruby
string. We allocate it, so it is `mrb_str_new_capa(mrb, length +
kWidePadding)` with the length set to `length` - the padding is real
because we asked for it, and nothing has to test for it afterwards.

That also settles the short string. mruby keeps one of those inside the
`RString` object, where there is no room behind it; asking for the
padding forces the allocation instead.

**A test copies.** A test hands over an mruby string, which has no page
behind it. The binding copies it into a buffer of `length +
kWidePadding`, so the padding is real and ASan can see the wall - a page
that merely exists is invisible to a sanitizer and would hide an
overrun rather than show it. `rake test` is the debug build, so the
suite gets that for nothing.

`kWidePadding` is 64, which covers AVX-512, and every wide reader
carries `static_assert(width <= kWidePadding)`.

This is simdjson's answer. `SIMDJSON_PADDING = 64` in `base.h`, its
`padded_string` allocates `length + SIMDJSON_PADDING`, and each reader
asserts against it.

mruby-fast-json answers the case this tree does not have. It takes Ruby
strings it did not allocate, so it asks whether the read stays inside one
mapped page - `last % page_bytes + SIMDJSON_PADDING < page_bytes` - and
copies when it does not. Worth knowing and not worth carrying: a rule
with no caller here is a rule that goes stale unread.

Both ways answer the same, and a test holds them against each other
over all 256 bytes at every length.

## A byte set is a table, and the table makes its own vector form

The set is written once as `std::array<bool, 256>`, from the ABNF.
`low_nibble_bits_of` computes the 16 byte table the vector code needs,
at compile time, from that same array. Nobody writes a set twice.

Neither compiler vectorizes the scalar form: a 256 entry table is a
gather, and gcc says so. The set written as nine range comparisons is
not vectorized either, and measured slower than the table, 174 ns
against 117. So the intrinsic is written, for AVX2 and NEON at once,
with the table as the third branch.

Measured in the tree, the twelve field names of a request from Chrome:
90.9 ns byte by byte, 37.7 ns with the wide read.

The technique is simdjson's, and it was chosen over the one the fast
servers use. picohttpparser, and so h2o and libreactor, scan with
`_mm_cmpestri`, which holds eight ranges; `tchar` needs nine, so
picohttpparser scans only for control bytes and checks the token
against a table byte by byte. `_mm_cmpestri` also has no counterpart in
NEON.

## What a request may cost

The fastest row this project has measured is in the archive, in
`bench/results/forgecore.log`: 10 165 746 requests a second over
HTTP/2 on one thread, so 98 nanoseconds per request and core. That is
the yardstick, until a row replaces it.

Read what that row measured before holding a number against it: no
TLS, no body, and an answer whose every field came out of the HPACK
table. It is the cost of moving frames, and a request that does work
costs more.

HTTP/2 reaches it partly because a field name arrives as an index and
has no bytes to check. HTTP/1.1 reads every name off the wire, so the
same work costs it more, and that is where it is worth removing.
