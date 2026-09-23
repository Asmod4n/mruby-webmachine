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

No class, no method and no algorithm that the standard library has is
built here. `std::string` is where output goes: there is no `Out`, no
writer, no builder. `std::span` is a set of buffers: there is no
`Spread`. A search is `find`, a comparison is `==` or
`std::ranges::equal`, a number is `std::to_chars`. No struct is built
that POSIX, C or C++ already have under another name: a pointer and a
length are a `std::span`, a `std::string_view` or an `iovec`, a time is
a `timespec` or a `std::chrono` type. Where a SIMD form is measured
faster, it stays, and the standard library form is its fallback: never
a second hand-written loop beside it. This holds in every
repository a session works in, and a speed number does not buy an
exception: a faster hand-made form goes to the owner as a question,
with the measurement, before it is written.

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

Every place that touches bytes is asked whether SIMD makes it faster,
in this order: first the form the compiler vectorizes by itself, then
`std::experimental::simd` (`std::simd` under its C++26 name), and last
intrinsics. What stays is whichever is faster, measured in one binary,
and safe: no read or write past what the code owns. A standard library
form that does the same work is the floor, and the floor is always
there: it is the fallback of every SIMD form, a test holds each SIMD
form to it, and a measurement names it as the arm to beat. There is
never a second hand-written loop beside it.

An intrinsic is written only where the first two are slower, both arms
built with the same `-march=`. Then it is written for AVX2 and for
NEON at once, with the standard library form as the third branch. AVX2
and NEON are the floor of what such code may use.

The NEON form is tested under qemu: built for aarch64 by clang with
`--target=aarch64-linux-gnu`, because no signed package gives a g++ 16
for aarch64 on this distribution, and run with `qemu-aarch64-static -L /usr/aarch64-linux-gnu`. The same
test that holds the x86 forms to the standard library form holds the
NEON form to it. A NEON form that no test has run is not written. qemu
answers whether the form is right, never how fast it is: a time read
under qemu is not a measurement, and the NEON speed stays unmeasured
until an aarch64 machine reads it.

AVX-512 is allowed on top of that where the machine has it (`avx512f`,
`avx512bw`, `avx512vl`) and the measurement says it is faster. The
AVX-512 form never stands alone: the AVX2 form and the standard library
form stay below it as its fallbacks.

## Every speed question is asked of every build

A comparison runs in each permutation of:

- the compiler: g++ and clang, each the newest release;
- the level: `-Os` and `-O2`; `-O3` lost too often to stay a column;
- the instruction set: `-march=x86-64-v4`.

That is four binaries for each kind of test, and one table with a column
for each. x86-64-v3 is no longer a column: the owner took it out. An arm
that one of them does not compile is a cell in that table with the
error, and is never left out without a word. An arm is chosen only when
it is the faster one across the table, not in one column.

The compilers are signed packages from an apt source, and nothing else:
apt.llvm.org for clang, the ubuntu-toolchain-r PPA for g++. A compiler
is never built here. The newest of each that such a source has is the
default: `gcc`, `g++`, `cc` and `c++` name g++ 16, which has the
reflection this tree needs, and `clang` and `clang++` name the newest
clang. clang builds against the libstdc++ of g++ 16: the Google
Benchmark package is built against libstdc++, and a binary built against
libc++ does not link with it, because libc++ names its types
`std::__1::`.

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
and a check that mrbtest does not count is a check nobody sees. A
failed `static_assert` is worse than uncounted: the compiler stops,
mrbtest is never linked, and every other check of the suite goes
unread.

The compiler still does the work. A binding writes `constexpr bool
answered = f();` and hands `answered` to the `.rb` file. That is a
constant expression, so the compiler evaluates every case, refuses
undefined behaviour on the path it walked, and refuses a function that
stopped being constant-evaluable. A wrong value is then one red line
among the others.

`static_assert` stays where nothing else can find the bug.

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

No version knows another. `Http2` never names `Http1`, and the state of
one version never stands in the header of another. This is the rule the
archive broke, and it is why this tree exists: there, HTTP/2's stream
and connection state were declared in HTTP/1.1's header, and the file
that frames HTTP/2 defined methods of the HTTP/1.1 class. Two versions
with one name.

What follows from that is the whole reason for the split. Anything a
version answers by itself exists in that version alone: the archive
serves its docroot from a method of the HTTP/1.1 class, so a request
over HTTP/2 is never served from disk. Not because somebody forgot it,
but because there was no HTTP/2 for it to live in.

The check is mechanical and belongs in the suite: a grep for one
version's name in another version's files finds nothing.

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

An application author sees webmachine and nothing else. The callbacks
carry webmachine-ruby's names one for one - content_types_provided,
charsets_provided, encodings_provided, languages_provided,
resource_exists?, generate_etag, last_modified, delete_resource,
create_path, process_post, finish_request - and the graph's nodes carry
its letters. A resource that runs under webmachine-ruby runs here.

Every server has an application. A docroot is one, written in C++; the
directory listing is one, written in Ruby and compiled into the binary;
what an operator points at with --app is one, read from a file. They
answer the same callbacks, and the graph runs on the answers without
seeing which language gave them.

So the callbacks are a C++ interface that a Ruby binding implements, and
not an mruby shape that C++ has to imitate. A request answered by the
C++ application makes no Ruby object at all, and the archive answers
those from its konst tier without entering a VM. No type here may assume
a VM, and nothing is built because Ruby might ask for it later.

That is also why a shape this tree invents may not stand where
webmachine has one. Two did and are gone: a Representation that bundled
a media type, a coding, a language and an entity tag, where webmachine
picks four values from four separate callbacks and bundles nothing; and
a Resource holding a function pointer, where a callback is answered by
whoever has an answer. Inside, `Http` keeps the words RFC 9110 uses - a
status is a status and not a code - because that is where the RFC is the
specification. The line is what the author sees.

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

The order is fixed, and it is the same for every listener:

1. `bind` the address the ask names.
2. `listen`, and a failure here ends the boot.
3. Ask the kernel: `getsockname` for the address and the port of this
   socket, and `getifaddrs` where the address is a wildcard.
4. Spell the URL out of those answers and write it into `conf.url`.
5. Run `app.ready`.

Nothing is reported before step 2 succeeded. A URL that names a socket
which never began to listen is a URL a client dials into nothing.

    app.ready do
      puts app.conf.url        # http://0.0.0.0:39241, the kernel's port
    end

Every listener is asked for itself. Two listeners have two ports, and a
port that was written once and a port the kernel chose are read the same
way, because the code that reads them cannot know which was which.

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

## A wildcard is not an address, so the addresses are listed

`0.0.0.0` and `::` say "every address this machine has". `getsockname`
answers with the wildcard, because that is what was bound, and no client
can connect to it. So the server also lists the addresses that wildcard
stands for, and `app.ready` reads them:

    app.ready do
      puts app.conf.url        # http://0.0.0.0:39241, what was bound
      app.urls.each { |url| puts url }
                               # http://127.0.0.1:39241
                               # http://192.0.2.2:39241
    end

`conf.url` keeps the wildcard, because that is the answer to what was
bound, and a second value cannot be the same thing. `app.urls` is the
list a client can use. Where the ask named one address, the list holds
that one address and says the same thing twice, which is what a caller
that does not want two cases needs.

`getifaddrs` makes the list. It runs once at boot and not on the serving
path, so it needs no ring operation. The rows are filtered: the family
the listener bound, `IFF_UP` and `IFF_RUNNING`, and nothing else is
dropped - the loopback address belongs in the list, because a test
connects to it.

This tree writes `getifaddrs` and slipstreamIO carries it to the
platforms that have none. That is what slipstreamIO is: it gives every
platform the Linux shape of a thing, and the shape is the whole point -
`slipstream_inotify` is inotify's three calls, its mask bits and its
record; `slipstream_signal` is signalfd's descriptor; `slipstream_tmpfile`
is `O_TMPFILE` where it exists and `mkstemp` plus `unlink` where it does
not. Addresses go the same way, and the underside on Windows is
`GetAdaptersAddresses`. At 53ee540 the gem carries no such header yet -
`grep -i ifaddr` over it finds nothing - so the call is written against
`getifaddrs` and the seam moves under it, the way the twelve `__sys_*`
wrappers already move under liburing.

Measured here, and this machine has no IPv6 at all, so the rows that
carry one are not measured and are not claimed:

    lo    ipv4 127.0.0.1  scope_id=0 up=1 running=1 loopback=1
    eth0  ipv4 192.0.2.2  scope_id=0 up=1 running=1 loopback=0

An IPv6 link-local address carries a zone, and a URL that holds one is
RFC 6874: `http://[fe80::1%25eth0]:8080`, with the "%" written "%25".
RFC 3986 allows no zone inside an IP-literal, which is why RFC 6874
exists and why a browser refuses such a URL where curl takes it. The RFC
is not in `refs/` yet; it arrives in the commit of the first function
that reads it.

The list is true when `ready` runs. An address that arrives later - DHCP,
an interface that comes up, a laptop that leaves one network for another
- is not in it. That is a snapshot, and it is what this tree has today.

Every platform can say when the list changed, and each says it in
another shape. This is written down because the shape decides what
slipstreamIO has to build, and one of the three already fits the ring:

- **Linux.** A netlink socket, bound to `RTNLGRP_IPV4_IFADDR` and
  `RTNLGRP_IPV6_IFADDR`. It is a descriptor, so it is a read the ring
  carries like any other.
- **Windows.** `WSAIoctl` with `SIO_ADDRESS_LIST_CHANGE`, which the
  documentation says to issue overlapped because it blocks. It completes
  when the address list of that socket's family changes, exactly once,
  and the application reissues it for the next change; then
  `SIO_ADDRESS_LIST_QUERY` reads the new list. An overlapped operation
  that completes later is a ring submission in every way that matters.
  The nearby error constants, `WSAENETUNREACH` and `WSAENETDOWN`, belong
  to `SIO_ROUTING_INTERFACE_CHANGE` losing its way, not to this.
- **Apple.** `nw_path_monitor` of the Network framework, macOS 10.14 and
  later, which calls a handler on a dispatch queue and carries the
  available interfaces in the path. It is a callback, so slipstreamIO has
  to give it a descriptor, the way it gives signals one.

Read from the documentation of the first two; the Apple page did not
render here and its rows come from the search summary, so they are
weaker than the others and are marked as such.

An operator who says "serve on every interface" does not want to restart
the service when an interface arrives. That is the requirement, and half
of it is already met by the kernel. Measured here:

    bound 0.0.0.0:41101 and listening, before the address exists
    10.99.0.1 added on lo:9 after bind and listen
    accepted from 10.99.0.1:59556

A wildcard socket serves an address that did not exist when `bind` and
`listen` ran. Nothing re-binds, nothing restarts, and this tree does
nothing for it. What goes stale is only what was reported: `app.urls`.
So the netlink socket exists to keep a list true, not to keep the server
reachable, and that is a much smaller thing than it first looked.

Where the ask names one address, the kernel refuses an address the
machine does not have - `bind 192.0.2.77: [Errno 99] Cannot assign
requested address` - and that refusal stands. An operator who named one
address asked for one address.

An application hears about a change through a second hook, and the hook
says which of three things happened:

    app.network_changed do |change|
      change.what        # :added, :changed or :removed
      change.interface   # "eth1"
      change.urls        # what this interface carries now, empty on :removed
      app.urls           # the whole list, already refreshed
    end

`ready` keeps its meaning: it runs once, when the server is up. This one
runs every time the machine's network changes, and it runs after
`app.urls` holds the new list, so the two can never disagree inside the
block.

The three are about the interface, and the addresses ride in the
payload. An interface that appears is `:added`. An interface that gains
or loses an address is `:changed`. An interface that goes away is
`:removed`. An interface that is still there and is down is `:changed`
with no URLs, because it is present and carries nothing - saying
`:removed` for it would make two different states read the same.

One change is one call. Netlink delivers a burst when an interface comes
up - the link, then every address on it - and an application that hears
four calls for one event has to decide which of them was real. So the
reactor reads everything the socket has, recomputes the list once, and
calls the hook once for each interface that differs. Decide, then do.

The block runs in the reactor's VM, between requests, the way every
other Ruby callback here does. A block that blocks stops the server, and
that is the application author's to know.

The name `network_changed` is this tree's own; no specification gives
one.

## Strict where it decides, lax where it only picks a status

An open question, with what is measured about it so far, so that the day
it is answered nobody starts from zero.

The archive read a media type with no grammar at all: the base is
everything before the first ";", trimmed. `parse_media_type` reads the
same bytes as `token "/" token`, and measured in one binary that costs
about 3.4 ns more on a type of this length.

Those 3.4 ns are not a trade anyone has to weigh, because the archive's
reader is not a candidate. It hands back `"utf-8"` with its quotes and
compares byte for byte, so it reads as different the two spellings RFC
9110 5.6.6 calls equal. What the 3.4 ns buy is a grammar and one
spelling of a value.

Two shapes were tried against the 3.4 ns and both lost. A wide scan that
returns the first byte that is not tchar - the nibble mask plus
`countr_zero`, so the end of the token and the grammar check are one
answer - read 11.1 ns. Splitting with `memchr` and then checking the run
with one wide pass read 17.7 ns, worse than everything. A token here is
4 to 12 bytes, and on a run that short one scalar pass beats several
vectorized ones: the table setup costs more than the bytes it reads.
Neither shape is in the tree.

What the numbers do not settle is where a refusal belongs. A refusal is
cheap when it runs once over the whole buffer and is not repeated per
field: the classifier reads 120 bytes in 6.75 ns and 8000 in 267, so a
600 byte header section is about one pass of 35 ns for every field at
once. Against that stands 3 ns for each field read strictly.

So laxness is a choice per field, and the line is what a wrong answer
costs:

- **Strict, always.** The value becomes a file name; it decides framing
  (Content-Length, Transfer-Encoding, a chunk size); it is compared for
  equality to decide something (an entity tag in If-Match, credentials);
  or it is written back into a response line. There a lax read is a
  traversal, a smuggled request, or an injected header.
- **Lax is allowed** where the only difference is which status comes
  back. A media type that matches nothing ends as 415 or 406 whether it
  was read strictly or not. The same holds for content codings, language
  tags and the qvalues of the Accept fields.

The precondition for laxness is that the bytes which are dangerous
everywhere - CR, LF, NUL, the control bytes - are refused before any
field parser runs lax, and picohttpparser refuses them. Not only off the
wire: `phr_is_field_name` and `phr_is_field_value` are there so that
what HPACK and QPACK hand over is held to the same definition, which is
what RFC 9113 8.2.1 asks of an HTTP/2 recipient. Nothing of ours stands
in front of that or beside it.

One shape was tried against that question. Read the field value once and
keep the structure in a register: a 32 byte block gives two masks from
one load - where the structural bytes of RFC 9110 5.6 stand (`/ ; = , "`)
and where a tchar stands - and then the parse walks the first mask with
`countr_zero`, while "is this run a token" is a bit test on the second.

One input of 23 bytes, four arms, one binary, medians of five:

| | |
|---|---|
| the archive's way: lax, several passes | 34.3 ns |
| this tree today: strict, several passes | 41.6 ns |
| one pass, lax | 28.7 ns |
| one pass, strict from the masks | **27.1 ns** |

So strictness is not what costs. The passes are. Where a value is read at
all, reading it once makes the grammar free: it is cheaper than the lax
reader that walks the bytes four times, and it refuses what that one
waves through.

Then the same idea was put to a whole request, and there it dies. A
request from Chrome is 416 bytes of header block. One pass over all of
it, both masks, is 33.9 ns. What the request actually costs to read is
this:

| what the graph asks for | |
|---|---|
| a plain GET: the Host that routed it, and nothing else | 12.6 ns |
| a conditional GET: Host, the If-None-Match list, If-Modified-Since | 49.1 ns |
| a negotiated GET: Host and Accept with its media types and q | 80.9 ns |
| every field of the request read | 415.9 ns |

The last row is the one nobody pays. This server looks at what it needs:
the graph walks on facts, and a fact is "is there an If-None-Match", not
what stands inside it; a value is read where a node needs it, and a Ruby
object is made where a resource asks for it. That is the archive's
`ReqFacts` - a struct of bools - and its `body_io_` memo, one object per
run, built on the first ask.

So a pass over the whole block pays for the 300 bytes of User-Agent and
sec-ch-ua that nobody reads, and for a plain GET it costs two and a half
times the whole parse. The premise of a whole-block lexer is that the
bytes get read anyway. They do not.

What survives is the narrow form: stay lazy, and read a value in one
pass on the day something asks for it.

Three things the spike does not settle, for the day it is built. It
covers a value of 32 bytes or less and falls back above that, and a block
loop needs the bit test to cross a block. It leans on picohttpparser
having refused every control byte already, which holds for every version
because phr_is_field_value is asked for the ones that arrive out of a
dynamic table. And it had a bug the
numbers would have carried: RFC 9110 5.6.6 allows OWS around the
semicolon, the spike read the space as part of the parameter name, and
`text/html; charset=utf-8` - which the tree reads correctly today - was
refused. A check caught it before the row was believed.

The line between strict and lax stays written down, because it answers a
different question - what a wrong answer costs, not what it saves.

## is_token exists twice on purpose, and a test holds the two together

picohttpparser already answers what a field name is, in
`phr_is_field_name` and `phr_is_lowercase_field_name`, and this tree
keeps `is_token` and `is_lowercase_token` beside them anyway. Measured in
one binary, medians of five, over the thirteen field names of a Chrome
request: 37.7 ns here against 90.8 in phr, and 37.5 against 167 for the
lowercase form. `findchar_fast` needs sixteen bytes before it does
anything and ten of those names are shorter, so phr walks them one byte
at a time.

The two cannot be merged, and that is a fact rather than a preference.
Every input in phr's own suite is placed with its last byte against an
unreadable page, which tests the promise that it never reads past what
it was given. Our classifier loads 32 bytes whatever the length. Put to
the same wall:

    phr_is_field_name      answered 1
    http::is_token         died with signal 11

So a test holds them together instead: every byte value, at every
position, at every length to 40, both pairs, 419841 comparisons, and
they agree. It moves into `test/` on the day mruby-phr is in the build.

## A gem that only the tests need is a test dependency

`spec.add_test_dependency` in `mrbgem.rake`, not `conf.gem` in a build
configuration. A gem in the configuration is in every build and in the
library that ships.

## A gem asks for C++20 only where the build has less

A gem that needs C++20 reads the build's C++ flags in its own
`mrbgem.rake`. Where they already name `-std=c++20` or a later standard,
it adds nothing. Only where they name none, or an earlier one, does it
add `-std=c++20`. A gem never forces its standard over a later one: this
build uses `-std=c++26 -freflection`, and a forced `-std=c++20` took the
reflection away from every file after it.

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

Each comparison has its own binary, and that binary holds the arms of
that comparison and nothing else. A second question is a second binary.
One kind of test per binary, too: a short request and a long one, a
field name and a path, are two tests and two binaries, even when the
arms are the same. The source may hold several tests, and the build
picks exactly one of them.
Measured in mruby-mustache: one arm read 315 and 327 ns, and the same
arm read 378 ns after an arm for another question was linked into the
same binary. Its code had not changed. Each arm that is added moves the
code of every other arm.

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

So: the alignment flags are always on, the arms of one comparison, and
only those, are compared inside one binary, and an absolute nanosecond
figure is never carried from one build to the next. Below roughly a
third, the clock cannot answer a question across binaries at all, and
`bench/instructions.sh` is what can - it counts what the binary
executed, and that count does not move.

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

## A server run is valid only when both sides are busy

The server has one thread. The client is htgen with one or two
threads, never more. `bench/floor.sh` is the form, taken from the
archive.

A run is valid only when the server and the client each use at least
90 percent of one core. A run where either stays below that measured
the waiting, not the server, and its numbers are not written down.

One server runs at a time. The archive and this tree are measured one
after the other, never side by side.

The connections number in the hundreds. Thirty-two do not keep one
server thread busy.

What is read is the share of the server's time that the kernel spends,
and the userland nanoseconds per request. htgen spends 99.9 percent of
its time in the kernel. The server's goal is 95 percent: what is left
for userland is the cost of this tree.

## A run is never pinned

No measurement pins a process or a thread to a core: no `taskset`, no
`sched_setaffinity`, no `cpuset`. The scheduler places the run.

## A limit is tested by a user the limit holds

Every limit this tree meets or sets is tested as a user with no
capability: not root, no CAP_IPC_LOCK, no CAP_SYS_NICE, no
CAP_SYS_RESOURCE. Root is not held to RLIMIT_MEMLOCK or RLIMIT_NICE,
so a test that runs as root sees no limit and proves nothing about
what the code does when it meets one.

`setpriv --reuid=65534 --regid=65534 --clear-groups` runs a command
as nobody, and `prlimit` sets the limit it runs under.

Measured on 6.18.44, RLIMIT_MEMLOCK 8192 KiB, one ring in each process:

| SQ entries | taken | refused |
|---|---|---|
| 32768 | rings 1 and 2, as nobody | ring 3, ENOMEM |
| 16384 | rings 1 to 5, as nobody | ring 6, ENOMEM |
| 32768 | as root, every time | - |
| 49152, 65536 | - | EINVAL, as any user |

So the budget belongs to the uid and not to the process: three
servers under one user share it, and a ring cannot know what the
others took. The kernel charges about 1.6 MiB for 16384 entries: 64
bytes for each SQE, 16 for each of twice as many CQEs, and the SQ
array. No ring takes more than 32768 SQ entries, whatever the budget.

## What is there to debug is there in a debug build

Everything that exists to debug is compiled only where `MRB_DEBUG` is
defined, and `conf.enable_debug` is what defines it. There is no macro
of this tree's own beside it: `MRB_DEBUG` is what gives the C and C++
side of mruby what it needs to be debugged, and a second switch would
only be a way for the two to disagree.

A Ruby backtrace is another thing and does not depend on it. Bytecode
compiled with `mrbc -g` carries its file names and line numbers, and an
exception raised in it has a full backtrace in every build. So the
error log has one in a release build too, as long as the application
was compiled with `-g`. That backtrace names Ruby frames and nothing
else: where C or C++ went wrong is read in a debug build.

So a release binary does not hold the code, and nobody can switch it on
in production. It costs nothing there, and it shows a client nothing
about the server. In the debug build and only there:

- the exception, its message and its backtrace on an error page;
- the path a request took through the decision graph.

What the operator reads stays in every build: the error log with the
request, the fingerprint and the exception with its backtrace, and
the `instance` and `fingerprint` on an error page, which name a record
of that log and nothing else.

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

`kWidePadding` is 64, which covers AVX-512. `kWideBlockBytes` is what
the scanner of this build loads at a time, and the suite checks that
the padding covers it.

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

## What is built while answering was built too late

A body that comes into existence during a request costs that request the
whole of its making, and no language makes that cheap. So the work moves
earlier, and there are only four places it can stand:

- **At build time.** `assets.cpp` in the archive computes the gzip of
  every entry, its CRC, its length and its entity tag when the pack is
  made. A request then picks one and sends it.
- **At `route.add`.** The field plan, the provided lists copied into
  padded storage and checked once, the compiled cache key. Everything
  the class declares is known there, and `def self.` gives way to a DSL
  so that it is data rather than a call.
- **At start.** The catalogue is opened, the templates are compiled,
  `Mustache::Template.compile` runs once and freezes its ops.
- **At the first request that needs it**, and then never again. That is
  the whole of what the cache is for.

What is left for the request itself is choosing and sending.

This is also why every answer of ours has a Content-Length. A length is
unknown only where a body is still being made, and the one body that is
legitimately still being made is an event stream. Everything else with
an unknown length is a body that was built too late.
