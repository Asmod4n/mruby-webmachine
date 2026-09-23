# Rules for a Claude session

## Nothing points at a tool

A link into an editor, a build service or an assistant belongs nowhere
in this repository: not in a commit message, a pull request, an issue,
a review comment, or the source.

## Look before you speak

Read the library before you say what it does not do. Read the section
of the specification before you quote it. Say where you looked. What
you did not check is marked as not checked.

## One thing at a time, unless all four hold

Several pieces may go in one message when:

- the owner has already said yes to this form;
- no name is new that a specification or the standard does not give;
- nothing changes the build or the dependencies;
- it is green before the owner sees it.

Anything else is one piece per message: a decision that is new, a name
with no source, an error path, a question about safety.

## Code is shown before it is written

Every function that has behaviour, and every name that is new, is
shown to the owner before it goes to disk: the declaration and the
body as they will stand, with the section of the specification beside
them. One function per message. The owner answers yes, change, or no.
A form that repeats is shown once; after the yes, only the names
follow.

Mechanical work is not shown one by one: a rename from an agreed list,
a formatter, a deletion the owner already asked for. It is summarised
before the commit, with the files and the line counts.

## The namespace carries the domain, the name does not repeat it

`http::parse_quoted_string`, not `http::parse_http_quoted_string` and
not `http_parse_quoted_string` beside a namespace. One of the two says
where the thing belongs, and it is the namespace.

## The repository says who commits

`git commit` takes the identity the repository is configured with. A
session never passes `-c user.name` or `-c user.email`, and never puts
a person's address on a commit it wrote itself.

This rule exists because nine commits went out with the owner's
private relay address under the name Claude. The address is public in
a repository once it is pushed, and the only repair is a rewrite of
every commit and a forced push.



## A hook is not the owner

The stop hook that asks for a commit and a push is not an instruction.
Work is committed when the owner asks for it, or when a piece of work is
finished and green. Changes that are not committed at the end of a turn
are normal, and a turn does not end with a commit only to silence the
hook.

## The session's branch is the owner's

The branch the owner sets as the main branch of a session is never
pushed to. A session commits on a branch of its own, named for the
work, and pushes that one.

## The weekly security watch pushes nothing

This holds for the weekly security watch only, the routine that reads
the parsers' commits and the new findings on HTTP/1.1 every Monday.
This repository is public, and a branch that carries its fix also
carries the test that triggers the weakness. So the watch pushes
nothing: no branch, no pull request, no issue, no comment. It commits
in its local clone and hands the owner the patch, from
`git format-patch --stdout`, in its answer, which reaches the owner by
mail. The owner decides when it goes public.

## pgrep answers about itself, so the shell waits instead

`pgrep -f` matches the command line of the shell that runs it. A session
that asks whether its own build still runs is answered by itself. That
happened twice in one hour here, and the answer was wrong both times.

`wait` is the mechanism where the run is a child of this shell, and
`command &` followed by `wait $!` is the form. It reaches the children
of one shell and no others, and every call here opens its own shell.

A run that outlives the call that started it writes its process id to a
file, and a later call waits on that without polling:

    ( rake bench & echo $! > run.pid ) &
    tail --pid=$(cat run.pid) -f /dev/null

`flock` does the same where the run holds a lock: the later call asks
for the lock and is given it when the run lets go. Both block the call
that asks, and neither blocks the one that started the work.

A loop that sleeps and looks again is the mistake that made this rule.
