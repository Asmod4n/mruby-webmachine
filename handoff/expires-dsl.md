# The expires DSL

What the owner decided about caching, written down for the session that
builds it. Every sentence under "Decided" is the owner's; "Open" is what
nobody has decided yet. The code that exists is named where it is.

## Decided

**Caching per method is gone.** A resource says, once, how long what it
answers may sit in a cache:

    class Article < Webmachine::Resource
      expires 15.min
    end

That is the whole DSL: one word, one duration, per resource. Nothing per
method, nothing per callback.

**The number goes to the cache process, not into a header first.** The
writer (the process that holds the LMDB environment read-write) is told
the freshness lifetime with every value it stores, and keeps a timeout
per key. `cache_datagram_header.freshness_lifetime` (`src/cache_datagram.h`)
is that number, in seconds, next to the route and the field the value is
for. Expiry is the writer's sweep, not a check at read time alone: a
reader still compares `until <= now` (`cache_asked`, `src/cache.c`), and
the writer removes what is past.

**Every value expires on its own.** The cache is not one blob per route;
it is one LMDB `MDB_DUPSORT` entry per route and field
(`kCacheFieldStatus` ... `kCacheFieldBody`, `src/cache.h`), and a body
of its own in `bodies`. So one field of a route can be gone while the
others are still fresh, and the flow, which knows which callback it is
in, asks for exactly the field it needs.

**Whether the cache is on is application configuration**, not a
resource's business. Off, the server keeps nothing; the resource's
`expires` still holds, because:

**The client gets `Expires` as a response header, always.** With the
server cache on or off, a response from a resource that says
`expires 15.min` carries `Expires:` fifteen minutes from now (RFC 9110
10.2.3 for the field, RFC 9111 4.2.1 for what a cache makes of it).
The owner's words: then the client caches, at least.

**Per app, a table of defaults by media type.** A resource that says
nothing takes the lifetime the app gives its `Content-Type`:

    app.expires "text/html"       => 5.min
    app.expires "application/json" => 0
    app.expires "image/*"          => 1.day

Both exist: the resource's word wins over the table, the table over
nothing. (The syntax above is a sketch; the `expires` word and the
per-type table are decided, the exact Ruby is not.)

**The way back is a broadcast.** When the writer expires or invalidates
a key, every io thread hears it: `cache_gone_datagram` (`route`, `field`,
`why` in {`kCacheExpired`, `kCacheInvalidated`, `kCacheEmptied`}) over a
socketpair per thread, direct descriptors, and it arrives as a
completion like every other completion and is handled where the others
are. Nothing polls.

**Forgetting is an API, as a library.** From everything down to one
value, RFC 9111 4.4 style and on demand:

    cache_forget_everything / cache_forget_route / cache_forget_value
    (src/cache_forget.h)

Two ways in, one API: `cache_forgetting_through(fd)` when a server is
running (an io thread holds LMDB read-only and cannot delete; the
writer does it on its behalf), `cache_forgetting_on(app_name, directory)`
when the gem is loaded with no server and opens the file itself. A
method says which of the two is the case and how to reach it.

**Invalidation on unsafe methods is a MUST** (RFC 9111 4.4): a
successful POST, PUT, DELETE, PATCH to a target forgets that route. The
flow knows its method; that is where it happens.

**Transactions are pooled and never freed.** A reader's read transaction
is reset and renewed after each send (`cache_sent`), not aborted and
recreated; the pool is two arrays, push and pop, grows and is never given
back (`cache_taken`, `cache_reader_closed`). The completion of the send is
where the transaction is given back, because that is when the bytes it
points at are no longer being read.

## What exists

- `src/cache.h`, `src/cache.c`: the reader. Field enum, `cache_key_of`
  (now also `cache::route_of` in `src/cache.hpp`), open, taken, asked,
  sent, closed. C, to become C++ (see HANDOFF.md).
- `src/cache_datagram.h`: the 16 byte store header and the 16 byte gone
  datagram.
- `src/cache_forget.h`, `.c`: the forgetting API, both ways in.
- `src/cache_file.h`: the file name both sides agree on.
- `src/webmachine.hpp` 170: `expires(const http1::Request &)` is already
  a callback on `Resource` - the webmachine-ruby one, which returns a
  time. The DSL word `expires 15.min` is a *duration*; the two have to
  be reconciled, see Open.

## Open

- **Ruby syntax of the DSL.** `expires 15.min` needs `Integer#min`,
  `#sec`, `#hour`, `#day` (a few lines of Ruby in the gem's mrblib) or
  another spelling. Not decided.
- **The callback and the word.** webmachine-ruby's `expires` callback
  answers a point in time per request; the DSL word is a lifetime per
  resource. One rule that could hold both: the word sets the lifetime
  the callback defaults to, and a resource that overrides the callback
  answers for itself. Not decided.
- **`Cache-Control` beside `Expires`.** RFC 9111 5.2.2.1 `max-age` says
  the same thing as a duration and wins over `Expires` where both are
  present (4.2.1). The owner said `Expires`, always; whether `max-age`
  goes out too is not decided.
- **What the writer does at the timeout**: delete the value, or mark it
  and let the sweep delete. Sweeps exist in the writer; the exact order
  is not written down here because the writer is not in this tree yet.
- **Where the app's media type table is read**: at `app.ready`, once, or
  per response. Not decided.
