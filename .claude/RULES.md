# Rules for a Claude session

## Nothing points at a tool

A link into an editor, a build service or an assistant belongs nowhere
in this repository: not in a commit message, a pull request, an issue,
a review comment, or the source.

## Look before you speak

Read the library before you say what it does not do. Read the section
of the specification before you quote it. Say where you looked. What
you did not check is marked as not checked.

## The owner permits a commit

1. Check. Name what you read.
2. Write into the working tree, compile, run the tests.
3. Show it. Green is one line. Red is shown in full.
4. The owner says whether it is committed.
5. After the yes: commit, run the tests again, then push.
6. Red after the commit: do not push, and say so.

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
