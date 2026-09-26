# The plan

How this tree is rebuilt, declared to the end. The owner reads it and
says yes, change, or no. Nothing below is written as code until then.

## 1. How every line is written

- The language is C++. No line of C. What C offers is not used. C is
  looked at only where the operating system has no other form.
- Where a C API must be used and a wrong length or a wrong pointer is
  possible, one C++ type wraps it. The wrapper takes its names from the
  C API: every underscore in a C name is a cut into namespace, class or
  method. `io_uring_prep_recv_multishot` is `io_uring::prep::recv_multishot`.
  `mdb_txn_begin` is `mdb::txn::begin`. Nothing outside the wrapper sees
  the C type.
- Where the C API has a setter and a getter, the wrapper has a field
  with `operator=` and a read. Where the C function is what a C++
  operator means in the standard, the wrapper has that operator:
  `mdb_get`/`mdb_put` is `dbi[key]`, `mdb_cursor_get(MDB_NEXT)` is
  `++cursor`, `io_uring_cq_advance` is `cq += n`,
  `io_uring_for_each_cqe` is `for (const cqe &c : ring.cq)`,
  `mrb_hash_get`/`mrb_hash_set` is `hash[key]`, `mrb_str_cat` is
  `str += view`. An operator never means something the standard does
  not give it.
- mruby is not wrapped. Of its C API, only the part where no mistake
  is possible is used: the calls that take and give `mrb_value`. The
  calls that take a pointer and a length
  (`mrb_str_new`, `mrb_str_cat`, `mrb_funcall_argv`,
  `mrb_ary_new_from_values`) get an overload under the same name in
  mruby-c-ext-helpers that takes `std::string_view` or
  `std::span<const mrb_value>`. Arguments come from `mrb_get_argv` as a
  span, never through the format string of `mrb_get_args`. mruby is
  built with `enable_cxx_exception`, so `mrb_raise` unwinds C++ frames.
- Everything that can be `consteval` or `constexpr` is. What is known at
  compile time is checked at compile time with `static_assert` in the
  tests: a wrong index, a wrong size, a wrong state does not compile.
- A function takes at most two arguments and returns a value. A method
  takes its object and one argument.
- Every name comes from a source outside this tree, in this order:
  1. the RFC the code implements (`refs/rfc*.txt`);
  2. webmachine, the original (`refs/webmachine`), and webmachine-ruby
     where its name is the better one;
  3. the C++ standard;
  4. the library under the wrapper (liburing, LMDB, miniz, zlib, POSIX).
  A function that has no name in any of these does not exist; its work
  moves to where the source puts it.
- A number stands in the tree only with a measurement behind it.
- Everything that goes into the tree is built by the mruby build chain.
  A one-off bench is built with the compiler alone and deleted after
  its result is logged.

## 2. The model

- An `Application` has routes. A route has a `Resource`. The resource
  class is written in C++ and given to mruby; the user subclasses it in
  Ruby. No Ruby is written in this tree.
- The user declares, once per resource, what the resource does: which
  media types it provides and accepts (`content_types_provided`,
  `content_types_accepted`), charsets, encodings, languages, how long
  its answers stay fresh (`expires`, one word, one duration), which
  request fields it wants (`request_headers`), and which decision
  callbacks it overrides (every callback of `webmachine_resource.erl`,
  under webmachine-ruby's names).
- At `add_route`, the server reads which callbacks and fields the
  resource declared and builds, once, the parser for that resource. The
  parser reads only what that resource needs.
- The decision core runs for every request. It is the reason the
  project exists.

## 3. The path of one request, declared to the end

Every name in this section is RFC 9110's, RFC 9112's or webmachine's.

1. **Bytes arrive** in a provided buffer of an `io_uring` multishot
   `recv` with bundles, on a direct descriptor. A request that does not
   end in one buffer keeps its buffers until it ends. A request larger
   than the limit is answered 431 or 413 (RFC 9110 15.5.14, 15.5.12).
2. **request-line** (RFC 9112 3). The `method` is read from its first
   bytes without a string compare: G is GET, H is HEAD, P is POST, PUT or
   PATCH by the second byte, D is DELETE, C is CONNECT, O is OPTIONS,
   T is TRACE, Q is QUERY (RFC 10008). Every method of RFC 9110 9.3,
   RFC 5789 and RFC 10008 is known; an unknown token is answered 501
   (RFC 9110 15.6.2).
3. **request-target** (RFC 9112 3.2): the bytes to the next SP, found
   with SIMD and a scalar fallback. The target names the route; the
   route names the resource; the resource names its parser.
4. **field-lines** (RFC 9112 5): the resource's parser reads the fields
   the resource declared and the fields the decision core needs
   (`Host`, `Content-Length`, `Transfer-Encoding`, `Accept*`,
   `If-*`, `Range`, `Expect`, `Connection`). Every other field is
   skipped over by position. The values the user asked for stand in
   `request_headers`, a Hash with Symbol keys, normalized. The user may
   ask later for the fields that were skipped; then they are parsed and
   stand under String keys. `raw_header_bytes` is copied once on
   request.
5. **message body** (RFC 9112 6): `Content-Length` or `chunked`, with a
   size limit at three levels (server, application, resource). A body
   the resource did not ask for is drained.
6. **decision core** (`webmachine_decision_core.erl`, v3b13 to v3o20):
   each node is a `constexpr` function that asks the resource or the
   request and tail-calls the next. The graph is data at compile time.
7. **cache** (RFC 9111): before the resource renders, the core asks
   the cache for the stored response of the cache key (method, target,
   `Vary` fields). Fresh: the stored response is sent. Stale or absent:
   the resource answers, and the answer is handed to the writer with
   its freshness lifetime. A successful unsafe method invalidates the
   target (RFC 9111 4.4). Every stored field expires on its own.
8. **response** (RFC 9112 4, RFC 9110 6): status-line, `Date`,
   `Content-Type`, `Content-Length`, `Vary`, `Expires`, `ETag`,
   `Last-Modified`, `Allow`, `Location`, `Content-Encoding` (gzip or
   deflate through miniz and zlib, RFC 9110 8.4.1), problem details for
   every error (RFC 9457) in four forms. Generated once per second per
   shape where the bytes repeat, never per request.
9. **send**: one `io_uring` `send` per answer, from the buffer the
   request came in or from the cache, on the direct descriptor. The
   access log record of the answer is written through the ring into
   one log file, in the format other servers write.

## 4. I/O

- liburing is the I/O layer, through slipstreamIO on every platform.
  Multishot accept, multishot recv with bundles, direct descriptors,
  `SINGLE_ISSUER`, `DEFER_TASKRUN`, `COOP_TASKRUN`, completions
  drained the way the last measurement decided. The workaround for the
  bundle fault of kernels before the fix is built in.
- One ring per thread; as many threads as the machine gives. No
  thread-local variable. Buffers are provided, never registered; the
  pools grow and shrink per thread.
- The clock is read once per wake and handed down as a value.
- A child thread or process reports back with a control message.
  Signals arrive through `signalfd` as a direct descriptor on the ring.
- The cache writer is its own process, spoken to over socketpairs as
  direct descriptors. Expiry and invalidation come back as datagrams,
  handled as completions.

## 5. What this tree keeps, renamed where a name has no source

| keeps | why |
|---|---|
| `refs/` | the specifications, verbatim |
| `src/http.hpp` | RFC 9110 negotiation, dates, conditionals, 167 test lines |
| `src/http1.hpp` | RFC 9112 request line and fields |
| `src/problem.hpp` | RFC 9457 |
| `src/flow.hpp` | the decision graph as data; nodes renamed to webmachine's `v3b13` form |
| `src/zip.hpp`, `src/fingerprint.hpp`, `src/config.hpp` | assets, fingerprint, `webmachine.toml`; renamed where needed |
| `test/` | every test that cites a section |
| the cache design (`handoff/expires-dsl.md`) | decided by the owner |
| `bench/results/` | the measurements |

Deleted and rebuilt under names from the sources: `serve.hpp`,
`walk.hpp`, `ring.hpp`, `stored.hpp`, `home.hpp`, `cache.c`,
`cache.h`, `cache_forget.c`, `cache_forget.h`, `cache_datagram.h`,
`cache_file.h`, `cache.hpp`, `tools/*`.

## 6. What the archive has and this tree does not yet

In the order they are brought over, each one a block with its RFC:

1. Request bodies and uploads with limits at three levels.
2. Static files: docroot, MIME types, content sniffing, ranges
   (RFC 9110 14).
3. Access and error logs through the ring into one file.
4. `compute` and `watch`: work off the request loop, with the heap
   only there.
5. Server-sent events, as a route kind of its own.
6. WebSocket (RFC 6455) with permessage-deflate (RFC 7692), and over
   HTTP/2 (RFC 8441).
7. HTTP/2 (RFC 9113, HPACK RFC 7541) on the same listener. "One HTTP,
   and the versions under it": the semantics stay one class; HTTP/2
   only turns them into frames.
8. TLS through the kernel, from mruby-tls once its branch is fuzzed.
9. h2spec and the WebSocket suite on every push.

## 7. Today

Each block ends green in the mruby build chain, and each block that
touches the request path is measured against the echo floor of this
machine: 572 400 answers per second at one request per connection,
and the block may cost at most fifteen percent of it.

- **Block A, HTTP.** RFC 9112 for every byte of every request state:
  the request-line, every method, the fields, the body, every error
  with its status. RFC 9110 for the response. One fixed resource, no
  webmachine yet, measured against the floor.
- **Block B, webmachine.** `webmachine_decision_core::handle_request`
  over `flow.hpp`, the resource with every callback, the parser built
  at `add_route`, the Ruby class from C++ through mruby's API. A
  resource written in Ruby answers over the socket.
- **Block C, cache.** RFC 9111 names over LMDB and the writer process.
  The measurement with the cache on: the Ruby resource is not called
  per request.
- **Block D**, in the order of section 6, as far as the day goes.

## 8. So that a person can maintain it

- One file per RFC section or webmachine module, named as the source
  names it.
- Every function name is in the source it implements; a reader with
  the RFC open finds it.
- `src/` has no comments. The tests say why, and cite the section.
- Every number in the tree has a measurement in `bench/results/`.
- The rules stand in `RULES.md` and `.claude/RULES.md`, and this file
  says what is built.

## 9. Open, for the owner

- The exact Ruby spelling of the DSL (`expires 15.min`, `request_headers`,
  the per-type table). The Ruby side is the user's; the class comes
  from C++.
- `Cache-Control: max-age` beside `Expires`, or `Expires` alone.
- The limit for a request head (one buffer, or a number the owner
  gives), and the three body limits.
