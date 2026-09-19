# RFC 9110 5.6.2 Tokens
# A token is a name without quotes: a header field name, a method, a
# parameter name. It holds letters, digits, and these 15 marks:
#     ! # $ % & ' * + - . ^ _ ` | ~
# Nothing else. No space, no tab, no colon, no bracket, no quote.
#
# The list below is read off the RFC and not built from the table under
# test, so a wrong table has something to disagree with.
TCHAR = %q{abcdefghijklmnopqrstuvwxyz}.bytes +
        %q{ABCDEFGHIJKLMNOPQRSTUVWXYZ}.bytes +
        %q{0123456789}.bytes +
        %q{!#$%&'*+-.^_`|~}.bytes

assert('is_tchar answers RFC 9110 5.6.2 for every one of the 256 bytes') do
  256.times do |byte|
    assert_equal TCHAR.include?(byte), Webmachine::SpecHttp.tchar?(byte),
                 "byte #{byte}"
  end
end

SECTION = 0
RULE    = 1
TITLE   = 2
ALLOWED = 3
STATUS  = 4
OFFSET  = 5
FOUND   = 6
EXCERPT = 7

UNKNOWN_PROBLEM       = 0
TCHAR_PROBLEM         = 1
QUOTED_STRING_PROBLEM = 2
QDTEXT_PROBLEM        = 3

# A log reader has to see which rule refused and which byte did it.
# Without the byte, a bad quote and a control character read the same.
assert('ParseError names the section, the rule and the byte') do
  e = Webmachine::SpecHttp.parse_error(QDTEXT_PROBLEM, "\"ab\x01c\"", 3)
  assert_equal 'RFC 9110 5.6.4', e[SECTION]
  assert_equal 'qdtext', e[RULE]
  assert_equal 'HTAB / SP / %x21 / %x23-5B / %x5D-7E / obs-text', e[ALLOWED]
  assert_equal 400, e[STATUS]
  assert_equal 3, e[OFFSET]
  assert_equal 1, e[FOUND]
end

# The read buffer goes back to the kernel after the feed, so the record
# keeps its own copy of the bytes it refused.
assert('ParseError copies the excerpt and leaves it raw') do
  e = Webmachine::SpecHttp.parse_error(QDTEXT_PROBLEM, "\"ab\x01c\"", 3)
  assert_equal "\"ab\x01c\"", e[EXCERPT]
end

# One long field value may not grow the record.
assert('ParseError holds at most 32 bytes of the text') do
  e = Webmachine::SpecHttp.parse_error(QDTEXT_PROBLEM, 'a' * 200, 100)
  assert_equal 32, e[EXCERPT].size
end

# Under offset 16 there is nothing in front of the byte to show.
assert('ParseError starts the excerpt at the text when the offset is small') do
  e = Webmachine::SpecHttp.parse_error(QDTEXT_PROBLEM, 'abcdef', 2)
  assert_equal 'abcdef', e[EXCERPT]
end

# A truncated read gives exactly this, and the record may not read past
# the text it was handed.
assert('ParseError reports byte zero when the offset is past the end') do
  e = Webmachine::SpecHttp.parse_error(QDTEXT_PROBLEM, 'abc', 9)
  assert_equal 0, e[FOUND]
  assert_equal 9, e[OFFSET]
end

# Problem zero is what ErrRec carries for a raise out of the app, which
# has no rule of ours behind it.
assert('problem zero names nothing and has no status') do
  e = Webmachine::SpecHttp.parse_error(UNKNOWN_PROBLEM, 'abc', 0)
  assert_equal '', e[SECTION]
  assert_equal '', e[RULE]
  assert_equal 0, e[STATUS]
end

# RFC 9110 5.6.2 and 5.6.4 refuse different things, and a reader has to
# see which one refused.
assert('each problem carries its own section and rule') do
  t = Webmachine::SpecHttp.parse_error(TCHAR_PROBLEM, 'a:b', 1)
  q = Webmachine::SpecHttp.parse_error(QUOTED_STRING_PROBLEM, 'ab', 0)
  assert_equal 'RFC 9110 5.6.2', t[SECTION]
  assert_equal 'tchar', t[RULE]
  assert_equal 'The field name is not valid', t[TITLE]
  assert_equal 'RFC 9110 5.6.4', q[SECTION]
  assert_equal 'quoted-string', q[RULE]
  assert_equal 'The field value is not valid', q[TITLE]
end

QS_RULE   = 0
QS_OFFSET = 1
QS_FOUND  = 2

# RFC 9110 5.6.4 Quoted Strings
# A field value in double quotes holds more than a token does: a space,
# a colon, a slash, and every byte from 0x80 up. Two bytes need a
# backslash in front of them to stand inside: the double quote itself
# and the backslash. A control byte may not stand there at all.

assert('parse_quoted_string gives back the quoted-string with both quotes') do
  assert_equal '"ab"', Webmachine::SpecHttp.parse_quoted_string('"ab"')
  assert_equal '""', Webmachine::SpecHttp.parse_quoted_string('""')
end

# A parameter list holds more after the value, and the caller has to
# know where this one ended.
assert('parse_quoted_string stops at the closing quote') do
  assert_equal '"ab"', Webmachine::SpecHttp.parse_quoted_string('"ab"; q=1')
end

# A backslash puts a quote inside the value, and the value keeps it.
assert('parse_quoted_string reads a quoted-pair') do
  assert_equal '"a\\"b"', Webmachine::SpecHttp.parse_quoted_string('"a\\"b"')
  assert_equal '"a\\\\b"', Webmachine::SpecHttp.parse_quoted_string('"a\\\\b"')
end

# These are the bytes a token refuses and a quoted-string takes.
assert('parse_quoted_string takes a space, a colon and a slash') do
  assert_equal '"a b/c:d"', Webmachine::SpecHttp.parse_quoted_string('"a b/c:d"')
end

# obs-text is allowed here and nowhere else.
assert('parse_quoted_string takes a byte above 0x7F') do
  assert_equal "\"a\x80b\"", Webmachine::SpecHttp.parse_quoted_string("\"a\x80b\"")
end

assert('parse_quoted_string refuses a value that does not open with a quote') do
  e = Webmachine::SpecHttp.parse_quoted_string('ab')
  assert_equal 'quoted-string', e[QS_RULE]
  assert_equal 0, e[QS_OFFSET]
end

# A truncated read and a value cut off by a peer look the same here.
assert('parse_quoted_string refuses a value that never closes') do
  e = Webmachine::SpecHttp.parse_quoted_string('"ab')
  assert_equal 'quoted-string', e[QS_RULE]
  assert_equal 3, e[QS_OFFSET]
end

# A backslash at the end asks for a byte that is not there.
assert('parse_quoted_string refuses a backslash with nothing behind it') do
  e = Webmachine::SpecHttp.parse_quoted_string('"ab\\')
  assert_equal 'quoted-string', e[QS_RULE]
  assert_equal 4, e[QS_OFFSET]
end

# A CR inside a field value is how request smuggling travels.
assert('parse_quoted_string refuses a control byte and names it') do
  e = Webmachine::SpecHttp.parse_quoted_string("\"a\rb\"")
  assert_equal 'qdtext', e[QS_RULE]
  assert_equal 2, e[QS_OFFSET]
  assert_equal 13, e[QS_FOUND]
end

# A backslash does not make a control byte allowed.
assert('parse_quoted_string refuses a control byte behind a backslash') do
  e = Webmachine::SpecHttp.parse_quoted_string("\"a\\\x01b\"")
  assert_equal 'qdtext', e[QS_RULE]
  assert_equal 3, e[QS_OFFSET]
  assert_equal 1, e[QS_FOUND]
end

PARAM_NAME  = 0
PARAM_VALUE = 1
PARAM_REST  = 2

# RFC 9110 5.6.6 Parameters
# A parameter is a name and a value behind a semicolon, appended to an
# item in a field value: the charset of a media type, the weight of an
# Accept entry. The value is a token or a quoted string. No space is
# allowed around the equals sign.
#
# The binding gives three strings for a parameter, nil when none is
# left, and two entries, the rule and the offset, for a refusal.

assert('parse_field_value_parameter reads a name and a token value') do
  p = Webmachine::SpecHttp.parse_field_value_parameter(';charset=utf-8')
  assert_equal 'charset', p[PARAM_NAME]
  assert_equal 'utf-8', p[PARAM_VALUE]
  assert_equal '', p[PARAM_REST]
end

# A caller walks a list of parameters, so each read says where it ended.
assert('parse_field_value_parameter gives back what is left') do
  p = Webmachine::SpecHttp.parse_field_value_parameter(';a=1;b=2')
  assert_equal 'a', p[PARAM_NAME]
  assert_equal '1', p[PARAM_VALUE]
  assert_equal ';b=2', p[PARAM_REST]
end

# The value keeps its quotes. Removing them needs a copy, and that is
# another function.
assert('parse_field_value_parameter reads a quoted value whole') do
  p = Webmachine::SpecHttp.parse_field_value_parameter(';charset="utf-8"')
  assert_equal '"utf-8"', p[PARAM_VALUE]
end

# Optional whitespace stands around the semicolon and nowhere else.
assert('parse_field_value_parameter steps over whitespace at the semicolon') do
  p = Webmachine::SpecHttp.parse_field_value_parameter(" \t; \tq=0.8")
  assert_equal 'q', p[PARAM_NAME]
  assert_equal '0.8', p[PARAM_VALUE]
end

# The grammar allows a semicolon with no parameter behind it.
assert('parse_field_value_parameter steps over an empty parameter') do
  p = Webmachine::SpecHttp.parse_field_value_parameter(';;charset=utf-8')
  assert_equal 'charset', p[PARAM_NAME]
end

assert('parse_field_value_parameter gives nil when nothing is left') do
  assert_nil Webmachine::SpecHttp.parse_field_value_parameter('')
  assert_nil Webmachine::SpecHttp.parse_field_value_parameter(';')
  assert_nil Webmachine::SpecHttp.parse_field_value_parameter("; \t")
end

# Two parsers that disagree about a space here read two different
# media types, and that is how a sniffing bug starts.
assert('parse_field_value_parameter refuses a space around the equals sign') do
  e = Webmachine::SpecHttp.parse_field_value_parameter(';charset =utf-8')
  assert_equal 'parameter', e[0]
  assert_equal 8, e[1]
end

assert('parse_field_value_parameter refuses a name that is not a token') do
  e = Webmachine::SpecHttp.parse_field_value_parameter(';"a"=1')
  assert_equal 'tchar', e[0]
  assert_equal 1, e[1]
end

assert('parse_field_value_parameter refuses a missing value') do
  e = Webmachine::SpecHttp.parse_field_value_parameter(';charset=')
  assert_equal 'tchar', e[0]
  assert_equal 9, e[1]
end

# The offset counts from the start of the text the caller handed over,
# not from the start of the quoted string inside it.
assert('parse_field_value_parameter counts the offset from the whole text') do
  e = Webmachine::SpecHttp.parse_field_value_parameter(";charset=\"a\rb\"")
  assert_equal 'qdtext', e[0]
  assert_equal 11, e[1]
end
