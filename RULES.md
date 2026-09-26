# Rules

## Write in Simplified Technical English

Everything written in this repository follows ASD-STE100.

- One thought per sentence.
- Short sentences.
- Active voice.
- Simple words. One word keeps one meaning.
- No metaphors, no idioms, no rhetorical questions.
- No word in capitals for emphasis.

## Everything is in English

## A rule states what holds

A rule says what, never how, why, or with what. A rule carries no
fact, no number, no history and no name of a tool. What exists as
code is a fact, not a rule.

## A declaration says what happens

The function name says what the function does. Each parameter name
says what is passed. The return type says what comes back. A reader
of the declaration alone knows the behaviour, and does not need the
body.

## Every name comes from a source outside this tree

A name is taken from the first of these that names the thing:

1. A specification or an RFC.
2. The origin of the code that is used.
3. The concept that the code implements, in the words of that concept.

No name is made here.

## A method name is a sentence

A name is a grammatical English sentence that says what happened or
what is done.

## A name claims nothing that does not happen

## No method hides anything

No function wraps one library call. No function changes an argument
unless its name says so. No function raises unless its name says so.
No function does two things.

## Every argument is const

There is no output parameter and no pointer that is written through.
A function that changes an object is a method of that object.

## Nothing is copied to pass it

A type that owns memory is passed by `const&`. A view and a scalar are
passed by value. A function that needs its own copy takes the value.

## A type has no constructor unless it needs one

A type is an aggregate. A constructor is written where the type cannot
hold a wrong value otherwise. An error derives from an exception
class.

## Use what a library already has

Nothing a linked library answers is written here a second time. No
class, no method and no algorithm that the standard library has is
built here. No struct is built that POSIX, C or C++ already have
under another name. No pointer arithmetic by hand.

A SIMD form has the standard library form as its fallback, and there
is never a second hand-written loop beside it. A hand-made form goes
to the owner as a question, with the measurement, before it is
written.

## An index is checked by the compiler

## Kernighan and Ritchie

`.clang-format` states the layout. A name is short where its scope is
short and says its purpose where its scope is long.

## An error goes to whoever can repair it

A fault of ours is raised, nothing catches it, and the process ends.
Everything else is a value, and the server keeps serving. A catch
names the exact type it can recover from. `main` and the body of a
thread catch what reaches them, record it, and end.

## An error is small where it travels and full where it is read

## An error object does not allocate

## Every error branch is marked unlikely

## The optimal case is optimized, and the cold path stays usable

The path that succeeds is the one that is made fast. A cold path does
not become pathologically slower for it. An error reaches the end of
its function at once.

## Comments live in tests, and they say why

`src/` carries no comments. A test carries comments, and they say why
the test exists. A string that reaches a log, an error page or a
client is not a comment.

## Asserts live in tests

No `assert` and no `static_assert` in `src/`, and none in a test
`.cpp`. Where the input of a function is a finite set, every member
of the set is checked. The expectation is written from the
specification and not from the table under test.

## Every function could run in a functional language as it is

A function takes values and returns a value. It reads no global,
writes no global, keeps no static. A clock is a value too. An effect
happens in one place, after a pure function decided it.

## Make the work easy for the compiler

Nothing is allocated, thrown or called indirectly on the path that
succeeds. A result is read as soon as it is made. A fixed size is
checked once, at the top. The address of a local does not leave the
function. A change that is only believed to be faster does not enter
the tree.

## The compiler vectorizes, and we check that it did

What the compiler did is read, never assumed, on every compiler and
every architecture. Every place that touches bytes is asked whether
SIMD makes it faster, in this order: the form the compiler vectorizes
by itself, the standard library's SIMD type, intrinsics. What stays
is whichever is faster, measured in one binary, and safe. A form that
no test has run is not written. A time read under an emulator is not
a measurement.

## Each part does one thing, and the others do not know how

A module owns the types of its dependency and shows them to nobody.

## One HTTP, and the versions under it

The semantics are one class. Each version turns them into bytes and
back, and adds nothing of its own. No version knows another. The
decision graph cannot see which version carried a request.

## A class is one defined kind of work

What a specification defines as one kind of work is one class, and
the specification names it. Work that no specification defines waits
until it has one. A shape this tree invents may not stand where
webmachine has one.

## An application author sees webmachine and nothing else

The callbacks carry webmachine-ruby's names one for one. A resource
that runs under webmachine-ruby runs here. Every application answers
the same callbacks, whatever language wrote it. No type assumes a VM.

## Nothing received is dropped without an answer

## A parser copies nothing

## A grammar is read, not remembered

`refs/` holds the specifications this tree implements, verbatim. A
claim about a rule is checked there before it is written. A
specification is downloaded in the same commit as the first function
that reads it.

## mruby has the methods CRuby has

A missing method is a missing gem. Never write around it.

## No bang methods

## A gem that only the tests need is a test dependency

## A gem never forces its C++ standard over a later one

## A clone is recursive

## Measure before, measure after

A number about speed comes from a measurement in this session, on
this machine, with both arms built the same way, in one binary, run
alternately, medians compared. The noise floor is measured first, and
a difference under it is not a difference. A number with no
measurement behind it is not written down. An absolute figure is
never carried from one build to the next.

## Every speed question is asked of every build

One binary for each build, one table with a column for each. An arm
that one build does not compile is a cell with the error. An arm is
chosen only when it is the faster one across the table. The old
implementation stays as an arm until the change is decided.

## The compilers are signed packages

A compiler is never built here.

## What builds the tree is the distribution's package

Nothing that builds this tree is installed from anywhere else.

## The bench takes the machine, and says how much of it

The measurement leaves one cpu to the rest of the machine. The run
gets priority over everything else this user owns, and the row
records whether it got it. The run is never root and never pinned.

## A server run is valid only when both sides are busy

A run where either side waits is not written down. One server runs at
a time.

## A limit is tested by a user the limit holds

## What is there to debug is there in a debug build

## Nothing sends stderr to /dev/null
