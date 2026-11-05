H1 = Webmachine::SpecHttp1
PROBLEM = H1.problems

def h1_request(lines)
  "GET / HTTP/1.1\r\n#{lines}\r\n"
end

# RFC 9110 5.5 Field Values
# field-value holds field-vchar (VCHAR and obs-text), SP and HTAB, and
# nothing else: no NUL, no CR, no LF, no other control byte, no DEL.
assert('a field value holds each byte that RFC 9110 5.5 allows, and no other') do
  wrong = []
  (0..255).each do |byte|
    allowed = byte == 0x09 || byte == 0x20 || (0x21..0x7e).include?(byte) || (0x80..0xff).include?(byte)
    text = h1_request("Host: a\r\nX-A: a#{byte.chr}b\r\n")
    got = H1.parse_request(text)
    parsed = got == text.bytesize
    wrong << byte if parsed != allowed
    wrong << byte if !parsed && got != [PROBLEM[:field_line], 31]
  end
  assert_equal [], wrong
end

# The wide form of the check over field lines reads a whole block and
# masks what lies past the end. It must answer what the standard library
# form answers, for every byte at every position and every length, with
# CR bytes behind the end that the mask has to hide.
assert('the wide check of field lines answers what the standard library form answers') do
  assert_equal 0, H1.field_line_forms_disagree
end

# RFC 9112 5.1: "No whitespace is allowed between the field name and
# colon." A server MUST reject such a request with 400.
assert('a field line with whitespace before the colon is refused') do
  assert_equal [PROBLEM[:field_line], 25], H1.parse_request(h1_request("Host: a\r\nAccept : x\r\n"))
  assert_equal PROBLEM[:field_line], H1.parse_request(h1_request("Host\t: a\r\n"))[0]
end

# RFC 9110 5.1: a field name is a token.
assert('a field name that is not a token is refused') do
  assert_equal PROBLEM[:field_line], H1.parse_request(h1_request("Ho(st: a\r\n"))[0]
  assert_equal PROBLEM[:field_line], H1.parse_request(h1_request(": a\r\n"))[0]
  assert_equal PROBLEM[:field_line], H1.parse_request(h1_request("Host a\r\n"))[0]
end

# RFC 9112 5.2: obs-fold is not allowed in a request. A server MUST
# reject it with 400 or replace it, and this server rejects it.
assert('a field line that starts with SP or HTAB is refused') do
  assert_equal [PROBLEM[:field_line], 33], H1.parse_request(h1_request("Host: a\r\nX-A: b\r\n c\r\n"))
  assert_equal PROBLEM[:field_line], H1.parse_request(h1_request("Host: a\r\nX-A: b\r\n\tc\r\n"))[0]
end

# RFC 9112 2.2: a bare CR is invalid; RFC 9110 5.5: a field value with
# CR, LF or NUL is invalid and is rejected.
assert('a bare CR, a NUL or a control byte in a field value is refused') do
  ["a\rb", "a\0b", "a\x01b", "a\x7fb"].each do |value|
    assert_equal PROBLEM[:field_line], H1.parse_request(h1_request("Host: a\r\nX-A: #{value}\r\n"))[0], value.inspect
  end
end

# RFC 9112 2.2: a line that ends with LF alone is not a field line here.
assert('a field line that ends in a bare LF is refused') do
  assert_equal PROBLEM[:field_line], H1.parse_request("GET / HTTP/1.1\r\nHost: a\nX: b\r\n\r\n")[0]
end

assert('a request with valid field lines parses to its end') do
  text = h1_request("Host: a\r\nX-A: b \t c\r\nX-B: \x80\xff\r\nX-C:\r\n")
  assert_equal text.bytesize, H1.parse_request(text)
end

# RFC 9112 3: a server that receives a request-target longer than it
# wishes to parse MUST respond with 414. RFC 9112 3 asks for at least
# 8000 octets of request line.
assert('a request line above its limit is refused with the problem of 414') do
  line_most, = H1.limits
  assert_equal 8000, line_most
  target = '/' + 'a' * (line_most - 'GET  HTTP/1.1'.bytesize)
  fits = "GET #{target[0..-2]} HTTP/1.1\r\nHost: a\r\n\r\n"
  assert_equal fits.bytesize, H1.parse_request(fits)
  assert_equal [PROBLEM[:uri_too_long], 0], H1.parse_request("GET #{target}a HTTP/1.1\r\nHost: a\r\n\r\n")
  assert_equal [PROBLEM[:uri_too_long], 0], H1.parse_request("GET /#{'a' * line_most}")
  assert_equal [PROBLEM[:not_whole_yet], 0], H1.parse_request("GET /abc")
end

# RFC 6585 5: 431 is for a header section that is too large in total, or
# for one field line that is too large.
assert('a header section above its limit is refused with the problem of 431') do
  _, section_most = H1.limits
  filler = "X: #{'a' * (section_most - 'X: '.bytesize - 2 - 'Host: a'.bytesize - 2)}\r\n"
  fits = "GET / HTTP/1.1\r\nHost: a\r\n#{filler}\r\n"
  assert_equal fits.bytesize, H1.parse_request(fits)
  over = "GET / HTTP/1.1\r\nHost: a\r\nX: b#{filler}\r\n"
  assert_equal [PROBLEM[:request_header_fields_too_large], 0], H1.parse_request(over)
  assert_equal [PROBLEM[:request_header_fields_too_large], 0],
               H1.parse_request("GET / HTTP/1.1\r\nX: #{'a' * section_most}")
  assert_equal [PROBLEM[:not_whole_yet], 0], H1.parse_request("GET / HTTP/1.1\r\nX: a")
end

# RFC 9112 3.2: a server MUST respond with 400 to an HTTP/1.1 request
# that lacks a Host header field, that has more than one Host field line,
# or that has a Host field with an invalid value.
assert('parse_host_field refuses a missing, a repeated and an invalid Host') do
  assert_equal 'a.example', H1.parse_host_field(h1_request("Host: a.example:8080\r\n"))
  assert_equal [PROBLEM[:host], 0], H1.parse_host_field(h1_request("Accept: x\r\n"))
  assert_equal [PROBLEM[:host], 0], H1.parse_host_field(h1_request("Host: a\r\nHost: a\r\n"))
  assert_equal [PROBLEM[:host], 0], H1.parse_host_field(h1_request("host: a\r\nHOST: b\r\n"))
  assert_equal PROBLEM[:host], H1.parse_host_field(h1_request("Host: a b\r\n"))[0]
  assert_equal PROBLEM[:host], H1.parse_host_field(h1_request("Host:\r\n"))[0]
  assert_equal PROBLEM[:port], H1.parse_host_field(h1_request("Host: a:99999\r\n"))[0]
end

# RFC 9112 3.2 asks for Host only in HTTP/1.1. An HTTP/1.0 request has
# no Host and is served.
assert('an HTTP/1.0 request without Host has no host and is not refused') do
  assert_nil H1.parse_host_field("GET / HTTP/1.0\r\n\r\n")
end

def h1_body_length(lines, most = 1000, version = 'HTTP/1.1')
  H1.parse_message_body_length("POST / #{version}\r\nHost: a\r\n#{lines}\r\n", most)
end

# RFC 9112 6.3 item 7: a request with neither Transfer-Encoding nor
# Content-Length has a message body length of zero.
assert('a request without Content-Length and Transfer-Encoding has no body') do
  assert_equal 0, h1_body_length('')
end

# RFC 9112 6.3 item 6: a valid Content-Length is the message body length.
# RFC 9110 8.6: Content-Length = 1*DIGIT, and a list of one value
# repeated may stand for that value.
assert('a valid Content-Length is the message body length') do
  assert_equal 5, h1_body_length("Content-Length: 5\r\n")
  assert_equal 5, h1_body_length("Content-Length: 005\r\n")
  assert_equal 5, h1_body_length("Content-Length: 5, 5\r\n")
  assert_equal 5, h1_body_length("Content-Length: 5\r\nContent-Length: 5\r\n")
  assert_equal 0, h1_body_length("Content-Length: 0\r\n")
end

# RFC 9112 6.3 item 5: an invalid Content-Length, or several with values
# that differ, is an unrecoverable error, and the server answers 400.
assert('an invalid Content-Length or two that differ are refused') do
  ['+5', '-1', '5a', '', ' ', '18446744073709551616', '5, 6', '0x5'].each do |value|
    assert_equal PROBLEM[:content_length], h1_body_length("Content-Length: #{value}\r\n")[0], value.inspect
  end
  assert_equal PROBLEM[:content_length], h1_body_length("Content-Length: 5\r\nContent-Length: 6\r\n")[0]
end

# RFC 9110 15.5.14: 413 for content larger than the server will process.
assert('a Content-Length above the limit of the server is refused with the problem of 413') do
  assert_equal 1000, h1_body_length("Content-Length: 1000\r\n")
  assert_equal [PROBLEM[:content_too_large], 0], h1_body_length("Content-Length: 1001\r\n")
end

# RFC 9112 6.3 item 4: a Transfer-Encoding whose final coding is not
# chunked MUST be answered with 400. This server reads no chunked body,
# so a final chunked coding needs a length: 411 (RFC 9110 15.5.12).
assert('a Transfer-Encoding is refused with 400, or with 411 when its final coding is chunked') do
  assert_equal [PROBLEM[:length_required], 0], h1_body_length("Transfer-Encoding: chunked\r\n")
  assert_equal [PROBLEM[:length_required], 0], h1_body_length("Transfer-Encoding: gzip, Chunked\r\n")
  assert_equal [PROBLEM[:length_required], 0], h1_body_length("Transfer-Encoding: gzip\r\nTransfer-Encoding: chunked\r\n")
  assert_equal [PROBLEM[:transfer_encoding], 0], h1_body_length("Transfer-Encoding: gzip\r\n")
  assert_equal [PROBLEM[:transfer_encoding], 0], h1_body_length("Transfer-Encoding: chunked, gzip\r\n")
  assert_equal [PROBLEM[:transfer_encoding], 0], h1_body_length("Transfer-Encoding: chunked\r\nTransfer-Encoding: gzip\r\n")
end

# RFC 9112 6.1: Transfer-Encoding overrides Content-Length, and a server
# MAY reject a message with both; it closes the connection after it.
assert('Transfer-Encoding with Content-Length is refused as the Transfer-Encoding row says') do
  assert_equal [PROBLEM[:length_required], 0], h1_body_length("Content-Length: 5\r\nTransfer-Encoding: chunked\r\n")
  assert_equal [PROBLEM[:transfer_encoding], 0], h1_body_length("Transfer-Encoding: gzip\r\nContent-Length: 5\r\n")
end

# RFC 9112 6.1: a server MUST treat an HTTP/1.0 message with
# Transfer-Encoding as having faulty framing.
assert('an HTTP/1.0 request with Transfer-Encoding is refused with 400') do
  assert_equal [PROBLEM[:transfer_encoding], 0], h1_body_length("Transfer-Encoding: chunked\r\n", 1000, 'HTTP/1.0')
end

# RFC 9112 9.3: an HTTP/1.1 connection persists unless the request
# carries the "close" connection option; an HTTP/1.0 connection persists
# only where the request carries "keep-alive". RFC 9110 7.6.1: connection
# options are case-insensitive and form a list.
assert('is_persistent follows RFC 9112 9.3') do
  assert_true H1.persistent?("GET / HTTP/1.1\r\nHost: a\r\n\r\n")
  assert_false H1.persistent?("GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n")
  assert_false H1.persistent?("GET / HTTP/1.1\r\nHost: a\r\nConnection: Upgrade, CLOSE\r\n\r\n")
  assert_false H1.persistent?("GET / HTTP/1.1\r\nHost: a\r\nConnection: x\r\nConnection: close\r\n\r\n")
  assert_true H1.persistent?("GET / HTTP/1.1\r\nHost: a\r\nConnection: closed\r\n\r\n")
  assert_false H1.persistent?("GET / HTTP/1.0\r\n\r\n")
  assert_true H1.persistent?("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n")
  assert_false H1.persistent?("GET / HTTP/1.0\r\nConnection: keep-alive, close\r\n\r\n")
end
