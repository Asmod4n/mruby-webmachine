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

## A gem that only the tests need is a test dependency

`spec.add_test_dependency` in `mrbgem.rake`, not `conf.gem` in a build
configuration. A gem in the configuration is in every build and in the
library that ships.

## No bang methods

No method with `!`. A question a caller may ask is a `?` predicate.
