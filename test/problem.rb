P = Webmachine::SpecProblem

# RFC 9457 3.1 Members of a Problem Details Object
# type, title, status and instance are the members a server sends here.
# "about:blank" is the type a problem has when it means no more than its
# status code (4.2.1), and then the title is the status's reason phrase.
assert('problem+json holds type, title and status as RFC 9457 3.1 names them') do
  assert_equal '{"type":"about:blank","title":"Not Found","status":404}', P.form(:json, 404, {})
end

# RFC 9457 3.1.2: status is a JSON number, not a string.
assert('problem+json writes status as a number') do
  assert_include P.form(:json, 503, {}), '"status":503'
end

# RFC 9457 3.1.5: instance is a URI reference for this one occurrence.
# RFC 8259 7: a quote and a backslash inside a string are escaped.
assert('problem+json escapes what it did not write itself') do
  json = P.form(:json, 500, { instance: 'urn:uuid:"a\\b' })
  assert_include json, '"instance":"urn:uuid:\"a\\\\b"'
end

# RFC 9457 Appendix B: one element per member, in the namespace
# urn:ietf:rfc:7807, inside an element named problem.
assert('problem+xml has the shape RFC 9457 Appendix B gives') do
  want = <<~XML
    <?xml version="1.0" encoding="UTF-8"?>
    <problem xmlns="urn:ietf:rfc:7807">
    <type>about:blank</type>
    <title>Not Found</title>
    <status>404</status>
    </problem>
  XML
  assert_equal want, P.form(:xml, 404, {})
end

# XML 1.0 2.4: < and & in character data are markup and must be escaped.
assert('problem+xml escapes the instance') do
  assert_include P.form(:xml, 500, { instance: 'a<b&c' }), '<instance>a&lt;b&amp;c</instance>'
end

# An error page carries the status and its reason phrase, and a value
# from outside goes through the HTML escaper before it reaches the page.
assert('the HTML page names the status and escapes the instance') do
  html = P.form(:html, 404, { instance: '<script>' })
  assert_include html, '<title>404 Not Found</title>'
  assert_include html, 'Reference &lt;script&gt;'
end

# RFC 2046 4.1.3: text/plain has no markup, so nothing is escaped.
assert('text/plain writes the values as they are') do
  assert_equal "404 Not Found\nReference a<b\n", P.form(:text, 404, { instance: 'a<b' })
end

# A page for a status with no instance is the same bytes every time, so
# the server can render it once at the start and send it from then on.
assert('a page without an instance does not change between renders') do
  %i[html json xml text].each do |form|
    assert_equal P.form(form, 503, {}), P.form(form, 503, {})
  end
end

# In a build with MRB_DEBUG the page also shows what was raised. RFC 9457
# 3.1.4 keeps debugging out of detail, so these are extension members
# (3.2), and an array in XML is a list of <i> elements (Appendix B).
assert('a debug build shows the exception, the message and the backtrace') do
  skip 'not a debug build' unless P.debug?
  raised = { exception: 'RuntimeError', message: 'x < y', backtrace: ['a.rb:1', 'b.rb:2'] }
  json = P.form(:json, 500, raised)
  assert_include json, '"exception":"RuntimeError","message":"x < y","backtrace":["a.rb:1","b.rb:2"]'
  assert_include P.form(:xml, 500, raised), "<backtrace>\n<i>a.rb:1</i>\n<i>b.rb:2</i>\n</backtrace>"
  assert_include P.form(:html, 500, raised), '<pre>x &lt; y</pre>'
end

# RFC 9562 5.4: a version 4 UUID is 128 random bits with the version
# (0b0100 in the high half of octet 6) and the variant (0b10 at the top of
# octet 8) written over them. RFC 9562 4 gives the urn:uuid: form.
assert('instance is a urn:uuid: of version 4 and variant 10') do
  assert_equal 'urn:uuid:ffffffff-ffff-4fff-bfff-ffffffffffff', P.instance("\xff" * 16)
  assert_equal 'urn:uuid:00000000-0000-4000-8000-000000000000', P.instance("\x00" * 16)
end

# RFC 9457 3.1.5: instance names this one occurrence, and 3.2 lets a
# problem carry members of its own: the fingerprint names the kind of
# failure, so one grep of the error log finds every occurrence of it.
assert('instance and fingerprint reach every form') do
  facts = { instance: 'urn:uuid:1', fingerprint: 'abc' }
  assert_include P.form(:json, 500, facts), '"instance":"urn:uuid:1","fingerprint":"abc"'
  assert_include P.form(:xml, 500, facts), "<fingerprint>abc</fingerprint>"
  assert_include P.form(:text, 500, facts), "Fingerprint abc\n"
  assert_include P.form(:html, 500, facts), '<p>Fingerprint abc</p>'
end

# RFC 9110 12.5.1: a request without Accept takes any media type, and the
# server's first offer is HTML. application/json is its own media type
# (RFC 6839 3.1), so a client that names it gets it. A client whose Accept
# matches nothing still gets text/plain, which every client can read.
assert('the form follows Accept') do
  assert_equal 'text/html; charset=utf-8', P.form_of(nil, false)
  assert_equal 'application/problem+json', P.form_of('application/problem+json', false)
  assert_equal 'application/json', P.form_of('application/json', false)
  assert_equal 'application/problem+xml', P.form_of('application/problem+xml', false)
  assert_equal 'text/html; charset=utf-8', P.form_of('text/plain;q=0.5, text/html', false)
  assert_equal 'text/plain; charset=utf-8', P.form_of('application/pdf', false)
end

# A browser that fetches an <img> sends image/* and nothing else. It gets
# the picture when there is one for the status, and text/plain otherwise.
assert('image/* gets the picture only where the pack has one') do
  assert_equal 'image/jpeg', P.form_of('image/*', true)
  assert_equal 'text/plain; charset=utf-8', P.form_of('image/*', false)
end

# RFC 9110 12.4.2: a qvalue is at most three decimals after a 0 or a 1.
assert('an Accept that does not parse is refused') do
  assert_equal :refused, P.form_of('text/html;q=2', false)
end

# APPNOTE 4.3: the pack is a zip of stored entries, 55 pictures. A JPEG
# starts with the SOI marker FF D8 FF.
assert('the pack reads as 55 stored JPEG entries') do
  entries = P.zip_entries
  assert_equal 55, entries.size
  assert_equal ['400.jpg', "\xff\xd8\xff"], entries.first
end

# APPNOTE 4.4.7: a stored entry carries the CRC-32 of its bytes, and one
# changed bit in 404.jpg does not match it.
assert('a changed byte in an entry is refused by its CRC-32') do
  assert_equal 'APPNOTE 4.4.7: the CRC-32 does not match the data', P.zip_entries(120498)
end

# APPNOTE 4.3.16: the end record states the comment length, so a pack cut
# short has no end record where the length says it is.
assert('a pack cut short has no end of central directory record') do
  assert_equal 'APPNOTE 4.3.16: no end of central directory record', P.zip_entries(-1, 1)
end

# The pack writes the finished <img> into an extra field of each entry
# (APPNOTE 4.5.1 lets a writer add its own), so the page joins no URL.
assert('a status with a picture gets its <img>, one without gets none') do
  assert_include P.picture(404), 'src="/error_assets/404.jpg"'
  assert_nil P.picture(299)
end

# The key is 32 bytes, written as 64 hex digits in webmachine.toml.
assert('a fingerprint key is 64 hex digits') do
  assert_true P.key_valid?('00' * 32)
  assert_false P.key_valid?('00' * 31)
  assert_false P.key_valid?('zz' + '00' * 31)
end

KEY = (0...32).map { |i| format('%02x', i) }.join

# BLAKE2b with a key and a 16 byte digest (RFC 7693), over each part with
# its length in front as 32 bits little endian. The expected value comes
# from Python's hashlib.blake2b over the same bytes, an implementation
# this tree does not share.
assert('the fingerprint is keyed BLAKE2b over the framed facts') do
  got = P.fingerprint(KEY, 'GET', '/a', 'to_html', 'RuntimeError', 500, ['a.rb:1', 'b.rb:2'])
  assert_equal '77039148941a9cbc90435a3d76eb78c6', got
  assert_equal 'd928247ed1e62081c7cb0086b2e8e0bb', P.build(KEY, 'bytecode')
end

# The length in front of each part keeps "/a" + "bc" and "/ab" + "c" apart.
assert('parts that join to the same bytes still differ') do
  one = P.fingerprint(KEY, 'GET', '/a', 'bc', 'E', 500, [])
  two = P.fingerprint(KEY, 'GET', '/ab', 'c', 'E', 500, [])
  assert_not_equal one, two
end

# Two installations with two keys give two fingerprints for one failure,
# so nobody can hold a fingerprint from one server against another.
assert('another key gives another fingerprint') do
  other = 'ff' * 32
  assert_not_equal P.fingerprint(KEY, 'GET', '/', 'c', 'E', 500, []),
                   P.fingerprint(other, 'GET', '/', 'c', 'E', 500, [])
end
