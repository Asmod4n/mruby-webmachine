C = Webmachine::SpecConfig
HEX = (0...32).map { |i| format('%02x', i) }.join

# The fingerprint key sits in webmachine.toml under [log], as 64 hex
# digits. The same key on every start keeps a fingerprint from yesterday
# pointing at the same failure today.
assert('[log] fingerprint_key is read as 32 bytes') do
  assert_equal HEX.to_sym, C.fingerprint_key_in("[log]\nfingerprint_key = \"#{HEX}\"\n")
end

# A file without the line is a valid configuration: the server then
# makes a key of its own at the start.
assert('a file without the key, or without [log], gives no key') do
  assert_nil C.fingerprint_key_in("[log]\nerror_file = \"/tmp/e\"\n")
  assert_nil C.fingerprint_key_in("[server]\nport = 8080\n")
end

# RULES.md: at the start there is no configuration to keep, so what the
# file gets wrong comes back as a value with the file's name in it.
assert('a wrong key or a broken file is refused with the reason') do
  assert_equal 'webmachine.toml: [log] fingerprint_key is not 64 hex digits',
               C.fingerprint_key_in("[log]\nfingerprint_key = \"abc\"\n")
  assert_equal 'webmachine.toml: [log] fingerprint_key is not a string',
               C.fingerprint_key_in("[log]\nfingerprint_key = 5\n")
  assert_true C.fingerprint_key_in("[log\n").start_with?('webmachine.toml: ')
end

# rake install writes where it put the pack of error pictures into
# [server] error_assets, and the server reads it from there.
assert('[server] error_assets names the pack') do
  path = '/usr/local/share/mruby-webmachine/error-assets.zip'
  assert_equal path.to_sym, C.error_assets_in("[server]\nerror_assets = \"#{path}\"\n")
  assert_nil C.error_assets_in("[log]\n")
  assert_equal 'webmachine.toml: [server] error_assets is not a string',
               C.error_assets_in("[server]\nerror_assets = 1\n")
end

# The times a connection waits carry the names of the nginx directives
# that set the same times, in whole seconds. A key that is not there
# keeps its default: 60 s for a head, a body and a send, 75 s for the
# next request, 5 s to linger.
assert('[server] reads the five times, and a missing one keeps its default') do
  assert_equal [60, 60, 60, 75, 5], C.timeouts_in("[log]\n")
  assert_equal [60, 60, 60, 75, 5], C.timeouts_in("[server]\n")
  text = "[server]\nclient_header_timeout = 1\nclient_body_timeout = 2\nsend_timeout = 3\n" \
         "keepalive_timeout = 4\nlingering_time = 5\n"
  assert_equal [1, 2, 3, 4, 5], C.timeouts_in(text)
  assert_equal [60, 60, 60, 9, 5], C.timeouts_in("[server]\nkeepalive_timeout = 9\n")
end

# RULES.md: at the start, what the file gets wrong comes back as a value
# with the reason, and the server does not start.
assert('a time that is no whole number, not above 0, or too long is refused') do
  assert_equal 'webmachine.toml: [server] send_timeout is not a whole number of seconds',
               C.timeouts_in("[server]\nsend_timeout = \"60s\"\n")
  assert_equal 'webmachine.toml: [server] client_header_timeout is not above 0 s',
               C.timeouts_in("[server]\nclient_header_timeout = 0\n")
  assert_equal 'webmachine.toml: [server] lingering_time is above 30 s',
               C.timeouts_in("[server]\nlingering_time = 31\n")
  assert_equal 30, C.timeouts_in("[server]\nlingering_time = 30\n")[4]
  assert_equal 'webmachine.toml: [server] a timeout is above 24 h',
               C.timeouts_in("[server]\nkeepalive_timeout = 86401\n")
  assert_equal 86400, C.timeouts_in("[server]\nkeepalive_timeout = 86400\n")[3]
end
