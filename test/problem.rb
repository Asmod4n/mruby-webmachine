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
