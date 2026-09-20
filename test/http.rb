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

# RFC 9110 5.6.7 Date/Time Formats
# One timestamp format a sender may generate: a fixed width of 29
# bytes, always in GMT, always with English month and day names.
# strftime and strptime follow the locale of the machine, so a German
# host would read Mai where the wire says May. The month names are a
# table here for that reason.

assert('parse_imf_fixdate reads the timestamp of RFC 9110 5.6.7') do
  assert_equal 784_111_777,
               Webmachine::SpecHttp.parse_imf_fixdate('Sun, 06 Nov 1994 08:49:37 GMT')
end

assert('parse_imf_fixdate reads the start of the epoch') do
  assert_equal 0, Webmachine::SpecHttp.parse_imf_fixdate('Thu, 01 Jan 1970 00:00:00 GMT')
end

# A timestamp past this one no longer fits in a signed 32 bit number,
# and the answer has to stay right.
assert('parse_imf_fixdate reads a timestamp past 2038') do
  assert_equal 2_147_483_647,
               Webmachine::SpecHttp.parse_imf_fixdate('Tue, 19 Jan 2038 03:14:07 GMT')
  assert_equal 4_102_444_800,
               Webmachine::SpecHttp.parse_imf_fixdate('Fri, 01 Jan 2100 00:00:00 GMT')
end

# The calendar of std::chrono knows the leap years, so this tree does
# not count them itself.
assert('parse_imf_fixdate reads the 29th of February in a leap year') do
  assert_equal 1_582_934_400,
               Webmachine::SpecHttp.parse_imf_fixdate('Sat, 29 Feb 2020 00:00:00 GMT')
end

assert('parse_imf_fixdate refuses the 29th of February in a common year') do
  e = Webmachine::SpecHttp.parse_imf_fixdate('Mon, 29 Feb 2021 00:00:00 GMT')
  assert_equal 'IMF-fixdate', e[0]
  assert_equal 5, e[1]
end

# A leap second is 60, and the second after it is the next minute.
assert('parse_imf_fixdate reads a leap second') do
  assert_equal 820_454_400,
               Webmachine::SpecHttp.parse_imf_fixdate('Sun, 31 Dec 1995 23:59:60 GMT')
end

# The rule that refused says which one it was, and the offset counts
# from the start of the timestamp and not from the start of the time.
assert('parse_imf_fixdate refuses an hour above 23') do
  e = Webmachine::SpecHttp.parse_imf_fixdate('Sun, 06 Nov 1994 24:49:37 GMT')
  assert_equal 'time-of-day', e[0]
  assert_equal 17, e[1]
end

assert('parse_imf_fixdate names the byte of a day that is not digits') do
  e = Webmachine::SpecHttp.parse_imf_fixdate('Sun, 0X Nov 1994 08:49:37 GMT')
  assert_equal 'DIGIT', e[0]
  assert_equal 5, e[1]
end

assert('parse_imf_fixdate names the byte of a month it does not know') do
  e = Webmachine::SpecHttp.parse_imf_fixdate('Sun, 06 Mai 1994 08:49:37 GMT')
  assert_equal 'month', e[0]
  assert_equal 8, e[1]
end

assert('parse_imf_fixdate refuses a month name it does not know') do
  e = Webmachine::SpecHttp.parse_imf_fixdate('Sun, 06 Mai 1994 08:49:37 GMT')
  assert_equal 'month', e[0]
end

assert('parse_imf_fixdate refuses a digit place that holds a letter') do
  e = Webmachine::SpecHttp.parse_imf_fixdate('Sun, 0X Nov 1994 08:49:37 GMT')
  assert_equal 'DIGIT', e[0]
end

# The two obsolete formats are read by their own functions, not by this
# one, and a cut off timestamp is not a timestamp.
assert('parse_imf_fixdate refuses anything that is not 29 bytes') do
  ['Sun, 06 Nov 1994 08:49:37',
   'Sunday, 06-Nov-94 08:49:37 GMT',
   'Sun Nov  6 08:49:37 1994',
   ''].each do |text|
    e = Webmachine::SpecHttp.parse_imf_fixdate(text)
    assert_equal 'IMF-fixdate', e[0], text
    assert_equal 0, e[1], text
  end
end

# RFC 9110 5.6.7 obs-date
# The first of the two formats a sender may no longer generate and a
# recipient must still accept. Its year has two digits, so the RFC puts
# the window on the reader: a timestamp that looks more than 50 years
# ahead means the most recent past year with the same two digits. The
# current year is an argument, so the answer does not change with the
# clock of the machine that runs the test.

assert('parse_rfc850_date reads the timestamp of RFC 9110 5.6.7') do
  assert_equal 784_111_777,
               Webmachine::SpecHttp.parse_rfc850_date('Sunday, 06-Nov-94 08:49:37 GMT', 2026)
end

assert('parse_rfc850_date reads every length of day name') do
  assert_equal 784_111_777,
               Webmachine::SpecHttp.parse_rfc850_date('Wednesday, 06-Nov-94 08:49:37 GMT', 2026)
end

# 94 read in 2026 is 2094, which is 68 years ahead, so it is 1994.
# 70 read in 2026 is 2070, which is 44 years ahead, so it stays.
assert('parse_rfc850_date puts a year more than 50 ahead into the past') do
  a = Webmachine::SpecHttp.parse_rfc850_date('Sunday, 06-Nov-94 08:49:37 GMT', 2026)
  b = Webmachine::SpecHttp.parse_rfc850_date('Sunday, 06-Nov-70 08:49:37 GMT', 2026)
  assert_equal 784_111_777, a
  assert_equal 3_182_489_377, b
end

# The same text read in a different year means a different year.
assert('parse_rfc850_date reads the window from the year it is given') do
  assert_equal 784_111_777,
               Webmachine::SpecHttp.parse_rfc850_date('Sunday, 06-Nov-94 08:49:37 GMT', 1999)
  assert_equal 3_939_871_777,
               Webmachine::SpecHttp.parse_rfc850_date('Sunday, 06-Nov-94 08:49:37 GMT', 2100)
end

assert('parse_rfc850_date refuses an IMF-fixdate') do
  e = Webmachine::SpecHttp.parse_rfc850_date('Sun, 06 Nov 1994 08:49:37 GMT', 2026)
  assert_equal 'rfc850-date', e[0]
  assert_equal 0, e[1]
end

assert('parse_rfc850_date names the rule and the byte that refused') do
  e = Webmachine::SpecHttp.parse_rfc850_date('Sunday, 06-Mai-94 08:49:37 GMT', 2026)
  assert_equal 'month', e[0]
  assert_equal 11, e[1]
  e = Webmachine::SpecHttp.parse_rfc850_date('Sunday, 06-Nov-94 24:49:37 GMT', 2026)
  assert_equal 'time-of-day', e[0]
  assert_equal 18, e[1]
end

# RFC 9110 5.6.7 obs-date
# The second obsolete format. It names no zone, and a recipient reads
# it as GMT. Its day is two digits or a space and one digit, which is
# the only place in the three formats where a field changes width.

assert('parse_asctime_date reads a two digit day') do
  assert_equal 784_975_777, Webmachine::SpecHttp.parse_asctime_date('Sun Nov 16 08:49:37 1994')
end

assert('parse_asctime_date reads a day behind a space') do
  assert_equal 784_111_777, Webmachine::SpecHttp.parse_asctime_date('Sun Nov  6 08:49:37 1994')
end

assert('parse_asctime_date refuses two spaces where the day stands') do
  e = Webmachine::SpecHttp.parse_asctime_date('Sun Nov    08:49:37 1994')
  assert_equal 'DIGIT', e[0]
  assert_equal 8, e[1]
end

assert('parse_asctime_date names the rule and the byte that refused') do
  e = Webmachine::SpecHttp.parse_asctime_date('Sun Mai  6 08:49:37 1994')
  assert_equal 'month', e[0]
  assert_equal 4, e[1]
  e = Webmachine::SpecHttp.parse_asctime_date('Sun Nov  6 08:49:60 199X')
  assert_equal 'DIGIT', e[0]
  assert_equal 20, e[1]
end

# RFC 9110 5.6.7: a recipient must accept all three formats. Only the
# first may still be sent.

assert('parse_http_date reads all three formats as the same moment') do
  assert_equal 784_111_777,
               Webmachine::SpecHttp.parse_http_date('Sun, 06 Nov 1994 08:49:37 GMT', 2026)
  assert_equal 784_111_777,
               Webmachine::SpecHttp.parse_http_date('Sunday, 06-Nov-94 08:49:37 GMT', 2026)
  assert_equal 784_111_777,
               Webmachine::SpecHttp.parse_http_date('Sun Nov  6 08:49:37 1994', 2026)
end

# The message names the format a sender is allowed to use, because the
# other two are the ones the sender should not have tried.
assert('parse_http_date answers with the fixdate rule when no format fits') do
  e = Webmachine::SpecHttp.parse_http_date('yesterday', 2026)
  assert_equal 'IMF-fixdate', e[0]
  assert_equal 0, e[1]
end

# RFC 9110 5.6.2 Tokens
# is_token reads a whole run at once. Where the caller can read 32
# bytes from the start of the run, it loads all of them and masks what
# lies behind the run, which is how it answers in one step instead of
# one step per byte. The two ways have to agree, byte for byte and
# length for length, or a field name would be refused on one machine
# and taken on another.

assert('is_token agrees with is_tchar for every byte, wide and narrow') do
  256.times do |byte|
    text = byte.chr
    assert_equal TCHAR.include?(byte), Webmachine::SpecHttp.token?(text, 64), "wide #{byte}"
    assert_equal TCHAR.include?(byte), Webmachine::SpecHttp.token?(text, 1), "narrow #{byte}"
  end
end

assert('is_token agrees with itself at every length up to 40') do
  (1..40).each do |length|
    [0, 9, 32, 44, 58, 65, 97, 126, 127, 128, 255].each do |byte|
      text = ('a' * (length - 1)) + byte.chr
      wide = Webmachine::SpecHttp.token?(text, 64)
      narrow = Webmachine::SpecHttp.token?(text, text.size)
      assert_equal narrow, wide, "length #{length} byte #{byte}"
      assert_equal TCHAR.include?(byte), wide, "length #{length} byte #{byte}"
    end
  end
end

# The field names a browser sends are all tokens, and the separators
# around them are not.
assert('is_token takes the field names of a request and refuses the rest') do
  %w[Host User-Agent Accept-Encoding Sec-Fetch-Mode X-Forwarded-For].each do |name|
    assert_true Webmachine::SpecHttp.token?(name, 64), name
  end
  ['Host:', 'User Agent', 'a(b)', 'a,b', "a\rb", ''].each do |bad|
    assert_false Webmachine::SpecHttp.token?(bad, 64), bad
  end
end

# A name longer than one wide load falls back, and must answer the same.
assert('is_token answers the same past 32 bytes') do
  long = 'x' * 40
  assert_true Webmachine::SpecHttp.token?(long, 64)
  assert_false Webmachine::SpecHttp.token?(long + ' ', 64)
end

# RFC 3986 3.2.2, which RFC 9110 7.2 uses for the Host field
# host       = IP-literal / IPv4address / reg-name
# reg-name   = *( unreserved / pct-encoded / sub-delims )
# unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
# sub-delims = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / ","
#              / ";" / "="
# A colon is not in the set: it separates the port. Square brackets are
# not either: they carry an IP-literal, which is read another way.
REG_NAME = %q{abcdefghijklmnopqrstuvwxyz}.bytes +
           %q{ABCDEFGHIJKLMNOPQRSTUVWXYZ}.bytes +
           %q{0123456789}.bytes +
           %q{-._~!$&'()*+,;=%}.bytes

assert('is_reg_name answers RFC 3986 3.2.2 for every byte, wide and narrow') do
  256.times do |byte|
    text = byte.chr
    assert_equal REG_NAME.include?(byte), Webmachine::SpecHttp.reg_name?(text, 64), "wide #{byte}"
    assert_equal REG_NAME.include?(byte), Webmachine::SpecHttp.reg_name?(text, 1), "narrow #{byte}"
  end
end

assert('is_reg_name agrees with itself at every length up to 40') do
  (1..40).each do |length|
    [0, 9, 32, 37, 45, 58, 91, 93, 97, 126, 127, 128, 255].each do |byte|
      text = ('a' * (length - 1)) + byte.chr
      wide = Webmachine::SpecHttp.reg_name?(text, 64)
      narrow = Webmachine::SpecHttp.reg_name?(text, text.size)
      assert_equal narrow, wide, "length #{length} byte #{byte}"
      assert_equal REG_NAME.include?(byte), wide, "length #{length} byte #{byte}"
    end
  end
end

# What a browser and a proxy put in the field, and what may not be
# there. A colon and a bracket are refused here and read elsewhere.
assert('is_reg_name takes the names a Host field carries') do
  %w[example.com www.example.com localhost sub.domain.test 10.0.0.1
     xn--bcher-kva.de a-b-c.example].each do |name|
    assert_true Webmachine::SpecHttp.reg_name?(name, 64), name
  end
  ['example.com:8080', '[::1]', 'exam ple.com', "example\rcom", 'a/b', ''].each do |bad|
    assert_false Webmachine::SpecHttp.reg_name?(bad, 64), bad
  end
end

# A token and a reg-name are different sets, and the two must not drift
# into each other.
assert('a reg-name and a token allow different bytes') do
  assert_true Webmachine::SpecHttp.reg_name?('(', 64)
  assert_false Webmachine::SpecHttp.token?('(', 64)
  assert_true Webmachine::SpecHttp.token?('^', 64)
  assert_false Webmachine::SpecHttp.reg_name?('^', 64)
end

HOST_NAME = 0
HOST_PORT = 1

# RFC 9110 7.2: Host = uri-host [ ":" port ]
# The field carries no scheme, so it cannot know the default port. The
# port is nil where the sender named none, and curl really sends it
# that way: http://127.0.0.1/ puts "127.0.0.1" in the field and
# http://127.0.0.1:8111/ puts "127.0.0.1:8111".

assert('parse_host reads a name with and without a port') do
  a = Webmachine::SpecHttp.parse_host('www.example.com', 64)
  assert_equal 'www.example.com', a[HOST_NAME]
  assert_nil a[HOST_PORT]
  b = Webmachine::SpecHttp.parse_host('www.example.com:8080', 64)
  assert_equal 'www.example.com', b[HOST_NAME]
  assert_equal 8080, b[HOST_PORT]
end

# An address is a reg-name as far as the grammar goes, so it needs no
# separate path. Whether it routes anywhere is another question.
assert('parse_host reads an IPv4 address like any other name') do
  a = Webmachine::SpecHttp.parse_host('10.0.0.1:80', 64)
  assert_equal '10.0.0.1', a[HOST_NAME]
  assert_equal 80, a[HOST_PORT]
end

# The brackets stay in the name: that is what the ABNF says, and it is
# what a configured address is compared against.
assert('parse_host keeps the brackets of an IP-literal') do
  a = Webmachine::SpecHttp.parse_host('[::1]', 64)
  assert_equal '[::1]', a[HOST_NAME]
  assert_nil a[HOST_PORT]
  b = Webmachine::SpecHttp.parse_host('[2001:db8::8a2e:370:7334]:443', 64)
  assert_equal '[2001:db8::8a2e:370:7334]', b[HOST_NAME]
  assert_equal 443, b[HOST_PORT]
end

# RFC 3986 3.2.3 writes port = *DIGIT, so an empty port is syntax and
# means the scheme decides.
assert('parse_host takes a colon with no digits behind it') do
  a = Webmachine::SpecHttp.parse_host('example.com:', 64)
  assert_equal 'example.com', a[HOST_NAME]
  assert_nil a[HOST_PORT]
end

assert('parse_host refuses a port no socket can take') do
  e = Webmachine::SpecHttp.parse_host('example.com:65536', 64)
  assert_equal 'port', e[0]
  assert_equal 12, e[1]
  e = Webmachine::SpecHttp.parse_host('example.com:80a', 64)
  assert_equal 'port', e[0]
end

assert('parse_host refuses a name that is not a name') do
  ['exam ple.com', "example\rcom", 'a/b', ''].each do |bad|
    e = Webmachine::SpecHttp.parse_host(bad, 64)
    assert_equal 'Host', e[0], bad
  end
end

# A reg-name holds no colon, so the first one starts the port and
# everything behind it has to be digits.
assert('parse_host splits at the first colon') do
  e = Webmachine::SpecHttp.parse_host('exa:mple.com:80', 64)
  assert_equal 'port', e[0]
  assert_equal 4, e[1]
end

# The bytes inside the brackets are checked and the address is not.
# nginx and h2o do the same: a literal that means nothing matches no
# route, so it ends as a 404 rather than a 400.
assert('parse_host checks the bytes of an IP-literal and not the address') do
  e = Webmachine::SpecHttp.parse_host('[zz]', 64)
  assert_equal 'Host', e[0]
  assert_equal 1, e[1]
  e = Webmachine::SpecHttp.parse_host('[::1', 64)
  assert_equal 'Host', e[0]
  assert_equal '[:::::1]', Webmachine::SpecHttp.parse_host('[:::::1]', 64)[HOST_NAME]
end

ELEMENT = 0
REST    = 1

# A helper, not a test: the walk gives one element and the rest, so a
# list is what you get by asking until it says there is no more.
def list_of(text)
  found = []
  rest = text
  while (got = Webmachine::SpecHttp.parse_list_element(rest))
    found << got[ELEMENT]
    rest = got[REST]
  end
  found
end

# RFC 9110 5.6.1.2 prints these three as valid and these three as
# holding no element. A list is written "a, b, c", and a sender that
# merges two values can leave an empty slot behind - "a,,b" or a comma
# at the end. A recipient has to step over those rather than see an
# element that is not there.
assert('parse_list_element walks the lists RFC 9110 5.6.1.2 prints') do
  assert_equal %w[foo bar], list_of('foo,bar')
  assert_equal %w[foo bar], list_of('foo ,bar,')
  assert_equal %w[foo bar charlie], list_of('foo , ,bar,charlie')
  assert_equal [], list_of('')
  assert_equal [], list_of(',')
  assert_equal [], list_of(',   ,')
end

# RFC 9110 5.6.1.1: OWS sits on both sides of the comma, so an element
# keeps neither.
assert('parse_list_element drops the whitespace around an element') do
  assert_equal %w[foo bar], list_of("  foo \t , \t bar  ")
  assert_equal ['a b'], list_of('  a b  ')
end

# RFC 9110 8.8.3: etagc is %x21 / %x23-7E / obs-text, and the comma is
# %x2C, so a comma inside the quotes of an entity tag is part of the tag
# and not a separator. Splitting on every comma would make three tags
# out of these two.
assert('parse_list_element keeps a comma that stands inside quotes') do
  assert_equal ['"a,b"', '"c"'], list_of('"a,b", "c"')
  assert_equal ['"33a64df5"', 'W/"67ab43"'], list_of('"33a64df5", W/"67ab43"')
  assert_equal ['text/html;x="a,b"', 'text/plain'], list_of('text/html;x="a,b", text/plain')
end

# A quote that never closes leaves nobody able to say where the element
# ends, so the element runs to the end of the value and whoever parses
# it says what is wrong with it. The walk cannot: it does not know what
# the element was meant to be.
assert('parse_list_element hands an unterminated quote over whole') do
  assert_equal ['"a, b'], list_of('"a, b')
end

# RFC 9113 8.2.3 lets HTTP/2 split a Cookie over several field lines,
# and every browser does it. A cookie is delimited by ";" and not by a
# comma, so the comma walk must not cut one up.
assert('parse_list_element leaves a cookie in one piece') do
  assert_equal ['session=abc123; prefs=dark'], list_of('session=abc123; prefs=dark')
end

# RFC 9113 8.1.1 names "the inclusion of uppercase field names" as one
# of the things that make an HTTP/2 message malformed, and 8.2.1 says
# an implementation that already checks a field name against RFC 9110
# 5.1 "only needs an additional check that field names do not include
# uppercase characters". So this is the token table with A to Z taken
# out, and nothing else changes.
UPPERCASE = %q{ABCDEFGHIJKLMNOPQRSTUVWXYZ}.bytes

assert('lowercase_token? answers RFC 9113 8.2.1 for every one of the 256 bytes') do
  256.times do |byte|
    want = TCHAR.include?(byte) && !UPPERCASE.include?(byte)
    assert_equal want, Webmachine::SpecHttp.lowercase_token?(byte.chr, 1), "byte #{byte}"
  end
end

# The two tables differ in the 26 letters and in nothing else.
assert('lowercase_token? refuses exactly what token? accepts in uppercase') do
  %w[content-type accept x-forwarded-for a 0].each do |name|
    assert_true Webmachine::SpecHttp.lowercase_token?(name, name.size), name
  end
  %w[Content-Type ACCEPT X-Forwarded-For A].each do |name|
    assert_true Webmachine::SpecHttp.token?(name, name.size), name
    assert_false Webmachine::SpecHttp.lowercase_token?(name, name.size), name
  end
end

# RFC 9113 8.2.1 exempts a pseudo-header field, whose name carries the
# one colon a field name may not otherwise hold. The colon comes off
# before the check, so the table never has to know about it.
assert('lowercase_token? refuses a colon, pseudo-header or not') do
  assert_false Webmachine::SpecHttp.lowercase_token?(':path', 5)
  assert_true Webmachine::SpecHttp.lowercase_token?('path', 4)
end

# The wide read answers what the narrow one answers, at every length a
# 32-byte load can straddle.
assert('lowercase_token? reads wide and narrow the same') do
  buffer = 'a' * 64
  (1..40).each do |length|
    name = buffer[0, length]
    assert_true Webmachine::SpecHttp.lowercase_token?(name, 64), length
    bad = name.dup
    bad[length - 1] = 'Z'
    assert_false Webmachine::SpecHttp.lowercase_token?(bad, 64), length
    assert_false Webmachine::SpecHttp.lowercase_token?(bad, length), length
  end
end

LOWERCASE = %q{abcdefghijklmnopqrstuvwxyz}.bytes

# RFC 9110 5.1: a field name is case-insensitive. The same holds for a
# media type and subtype (8.3.1), a charset (8.3.2) and a content
# coding (8.4.1). std::tolower answers none of them, because it reads
# the locale, so the fold is ours and it touches A to Z and nothing
# else.
assert('ascii_lowered folds A to Z and leaves the other 230 bytes alone') do
  256.times do |byte|
    want = UPPERCASE.include?(byte) ? byte + 0x20 : byte
    assert_equal want, Webmachine::SpecHttp.ascii_lowered(byte), "byte #{byte}"
  end
end

# The cheap fold is "set bit 5", and it is wrong here. '^' is 0x5E and
# '~' is 0x7E, so bit 5 alone turns one into the other - and both are
# tchar, so x-a^b and x-a~b are two field names a client may legally
# send. The same pairing catches '@' with '`' and '[' with '{'.
assert('equal_ignoring_case does not confuse the bytes that differ only in bit 5') do
  [%w[^ ~], %w[@ `], %w<[ {>, %w<] }>, %W[_ \x7f], %W[\\\\ |]].each do |low, high|
    assert_false Webmachine::SpecHttp.equal_ignoring_case(low, high), "#{low} #{high}"
  end
end

assert('equal_ignoring_case answers RFC 9110 5.1 for a field name') do
  assert_true Webmachine::SpecHttp.equal_ignoring_case('Content-Type', 'content-type')
  assert_true Webmachine::SpecHttp.equal_ignoring_case('IF-NONE-MATCH', 'if-none-match')
  assert_true Webmachine::SpecHttp.equal_ignoring_case('', '')
  assert_false Webmachine::SpecHttp.equal_ignoring_case('content-type', 'content-types')
  assert_false Webmachine::SpecHttp.equal_ignoring_case('content-type', 'content_type')
  assert_false Webmachine::SpecHttp.equal_ignoring_case('accept', 'accept-encoding')
end

# Every one of the 65536 byte pairs, against the fold itself, so the
# comparison and the fold cannot drift apart.
assert('equal_ignoring_case agrees with ascii_lowered on every pair of bytes') do
  wrong = 0
  256.times do |left|
    256.times do |right|
      want = Webmachine::SpecHttp.ascii_lowered(left) == Webmachine::SpecHttp.ascii_lowered(right)
      wrong += 1 if Webmachine::SpecHttp.equal_ignoring_case(left.chr, right.chr) != want
    end
  end
  assert_equal 0, wrong
end

ORIGIN    = 0
ABSOLUTE  = 1
AUTHORITY = 2
ASTERISK  = 3

# A short string that is only ever compared is carried as a number, not
# as bytes. Every method RFC 9110 9.1 defines fits in eight bytes -
# CONNECT and OPTIONS are the longest at seven - so the whole method is
# one integer, and a comparison is one instruction instead of a memcmp.
# The bytes go in little-endian order, so the number is the bytes.
assert('method_number packs the bytes of a method') do
  assert_equal 0, Webmachine::SpecHttp.method_number('')
  assert_equal 'GET'.bytes.each_with_index.map { |b, i| b << (i * 8) }.inject(0) { |a, n| a + n },
               Webmachine::SpecHttp.method_number('GET')
  assert_equal 'CONNECT'.bytes.each_with_index.map { |b, i| b << (i * 8) }.inject(0) { |a, n| a + n },
               Webmachine::SpecHttp.method_number('CONNECT')
end

# RFC 9110 9.1: the method is case-sensitive, so this must not fold.
# Carrying the bytes as a number gives that for nothing.
assert('method_number keeps the case of a method') do
  assert_not_equal Webmachine::SpecHttp.method_number('GET'),
                   Webmachine::SpecHttp.method_number('get')
end

# We do not serve WebDAV, so a method of more than eight bytes is not one
# of ours. It answers 0, which is what an absent method answers, and the
# graph turns that into 501 at B12.
assert('method_number refuses a method that does not fit') do
  assert_equal 0, Webmachine::SpecHttp.method_number('PROPPATCH')
  assert_equal 0, Webmachine::SpecHttp.method_number('VERSION-CONTROL')
  assert_true Webmachine::SpecHttp.method_number('OPTIONS') > 0
end

# RFC 9112 3.2 gives four forms, and the first sentence of that section
# says the form depends on the method as well as on the bytes. It has to:
# "example.com:80" satisfies both scheme ":" hier-part and uri-host ":"
# port, because a scheme allows dots. Only CONNECT separates them.
assert('request_target_form answers the four forms of RFC 9112 3.2') do
  assert_equal ORIGIN, Webmachine::SpecHttp.request_target_form('/index.html', 'GET')
  assert_equal ORIGIN, Webmachine::SpecHttp.request_target_form('/', 'GET')
  assert_equal ORIGIN, Webmachine::SpecHttp.request_target_form('/a?q=1', 'GET')
  assert_equal ABSOLUTE,
               Webmachine::SpecHttp.request_target_form('http://example.com/a', 'GET')
  assert_equal AUTHORITY, Webmachine::SpecHttp.request_target_form('example.com:80', 'CONNECT')
  assert_equal ASTERISK, Webmachine::SpecHttp.request_target_form('*', 'OPTIONS')
end

# RFC 9112 3.2.4: the asterisk-form is only used for a server-wide
# OPTIONS. RFC 9112 3.2.3: the authority-form is only used for CONNECT.
assert('request_target_form binds the asterisk to OPTIONS alone') do
  assert_nil Webmachine::SpecHttp.request_target_form('*', 'GET')
  assert_nil Webmachine::SpecHttp.request_target_form('', 'GET')
  assert_equal AUTHORITY, Webmachine::SpecHttp.request_target_form('*', 'CONNECT')
end

# A lowercase method is not the method, so it is not CONNECT and its
# target is read as absolute-form rather than as an authority.
assert('request_target_form does not fold the method') do
  assert_equal ABSOLUTE, Webmachine::SpecHttp.request_target_form('example.com:80', 'connect')
end
