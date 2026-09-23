# The specifications this tree implements

Verbatim copies, from `https://www.rfc-editor.org/rfc/rfcNNNN.txt`. They
are here so that a claim about a grammar is read rather than
remembered, and so that a session with no network can still read one.

Nothing here is edited. A file that differs from the RFC Editor's copy
is a bug in this directory, not a local decision.

    grep -n "absolute-path = " refs/rfc9110.txt

| RFC | what it decides here |
|---|---|
| 5234 | the ABNF the others are written in, and the core rules: ALPHA, DIGIT, HTAB, SP, VCHAR |
| 3986 | the URI. segment, pchar, query, reg-name, IP-literal, and remove_dot_segments |
| 9110 | HTTP semantics. The field grammars, the methods, the status codes, the conditional requests, the negotiation |
| 9111 | caching |
| 9112 | HTTP/1.1 on the wire. The request line, the four target forms, chunked transfer coding |
| 9113 | HTTP/2. Frames, the field validity rules, the cookie that arrives on several lines |
| 9114 | HTTP/3 |
| 7541 | HPACK, the field compression of HTTP/2 |
| 9204 | QPACK, the field compression of HTTP/3 |
| 6265 | cookies |
| 9457 | the problem document an error resource answers with |
| 6455 | WebSocket |
| 7692 | permessage-deflate |
| 8441 | WebSocket over HTTP/2 |
| 4647 | matching a language range against a language tag |
| 5646 | the language tag itself |
| 1951 | DEFLATE |
| 1952 | gzip |
| 8259 | JSON, for the escaping a problem document needs |
| 5789 | the PATCH method |
| 2046 | media types |
| 9651 | Structured Field Values, which Accept-Query is written in |
| 10008 | the QUERY method |
| 9562 | UUIDs. Version 4 and the urn:uuid: form, for the instance of a problem |

`APPNOTE.TXT` is PKWARE's .ZIP File Format Specification 6.3.10, from
`https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT`. It decides
how the pack of error pictures is read: the end record, the central
directory, the local headers and the CRC-32.

A specification this tree starts to implement is downloaded here in the
same commit as the first function that reads it.
