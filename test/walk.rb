W = Webmachine::SpecWalk

def request_of(method, target, fields = {})
  lines = fields.map { |name, value| "#{name}: #{value}\r\n" }.join
  "#{method} #{target} HTTP/1.1\r\nHost: a\r\n#{lines}\r\n"
end

# RFC 9110 15.3.1: a GET on a resource that exists and can be represented
# is 200, and the graph ends at O18b (multiple_choices? no).
assert('a GET on an existing resource is 200 at O18b') do
  assert_equal [200, 'O18b', 'text/html'], W.walk(request_of('GET', '/'))
end

# RFC 9110 12.5.1: without Accept the first type the resource provides is
# chosen; with Accept the one it names.
assert('Accept picks the representation') do
  assert_equal 'application/json', W.walk(request_of('GET', '/', 'Accept' => 'application/json'))[2]
end

# RFC 9110 15.5.7: a representation nobody accepts is 406, decided at C4
# before the graph asks whether the resource exists.
assert('an Accept that matches nothing is 406 at C4') do
  assert_equal [406, 'C4', nil], W.walk(request_of('GET', '/', 'Accept' => 'image/png'), exists: false)
end

# RFC 9110 15.5.5: a resource that does not exist is 404, and only POST
# may go on past L7.
assert('a missing resource is 404 at L7') do
  assert_equal 404, W.walk(request_of('GET', '/x'), exists: false)[0]
end

# RFC 9110 15.5.6: the default allowed methods are GET and HEAD.
assert('a method the resource does not allow is 405 at B10') do
  assert_equal [405, 'B10', nil], W.walk(request_of('POST', '/'))
end

# RFC 9110 13.1.2 and 15.4.5: If-None-Match with the current entity tag on
# a GET is 304.
assert('If-None-Match with the current tag is 304') do
  got = W.walk(request_of('GET', '/', 'If-None-Match' => '"v1"'), etag: '"v1"')
  assert_equal [304, 'J18'], got[0, 2]
end

# RFC 9110 13.1.1 and 15.5.13: If-Match with another tag is 412.
assert('If-Match with another tag is 412 at G11') do
  assert_equal [412, 'G11'], W.walk(request_of('GET', '/', 'If-Match' => '"v2"'), etag: '"v1"')[0, 2]
end

# RFC 9110 12.4.2: a qvalue above 1 is not valid, and the walk refuses.
assert('an Accept that does not parse is refused') do
  assert_equal :refused, W.walk(request_of('GET', '/', 'Accept' => 'text/html;q=2'))
end
