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
