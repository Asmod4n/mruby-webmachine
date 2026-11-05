require 'minitest/autorun'
require 'socket'
require 'fileutils'
require 'tmpdir'

# These tests run webmachine-serve and speak to it over a unix socket.
# The bindings of mrbtest reach the parsers; only a running server shows
# what the connection does with the bytes it receives.
class ServeTest < Minitest::Test
  SERVER = ARGV.fetch(0)
  HELLO = "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n".freeze

  def setup
    @dir = Dir.mktmpdir
    @path = File.join(@dir, 's.sock')
    @pid = spawn(SERVER, @path, out: File.join(@dir, 'out'), err: File.join(@dir, 'err'))
    100.times do
      break if File.socket?(@path)
      sleep 0.05
    end
    assert File.socket?(@path), "the server did not listen: #{File.read(File.join(@dir, 'err'))}"
  end

  def teardown
    Process.kill(:TERM, @pid)
    Process.wait(@pid)
    FileUtils.remove_entry(@dir)
  end

  def answers_of(bytes)
    found = []
    until bytes.empty?
      head, rest = bytes.split("\r\n\r\n", 2)
      break if rest.nil?
      length = head[/^content-length: *(\d+)/i, 1].to_i
      found << [head.lines.first.chomp, rest.byteslice(0, length)]
      bytes = rest.byteslice(length..) || ''
    end
    found
  end

  def read_to_end(socket)
    got = +''
    while IO.select([socket], nil, nil, 2)
      got << socket.readpartial(65_536)
    end
    flunk 'the server did not close the connection'
  rescue EOFError
    got
  end

  def read_for_a_while(socket)
    got = +''
    got << socket.readpartial(65_536) while IO.select([socket], nil, nil, 0.5)
    got
  rescue EOFError
    got
  end

  def ask(bytes)
    socket = UNIXSocket.new(@path)
    socket.write(bytes)
    answers_of(read_for_a_while(socket))
  ensure
    socket&.close
  end

  def test_a_route_answers_with_its_status
    assert_equal 'HTTP/1.1 200 OK', ask(HELLO).first.first
    assert_equal 'HTTP/1.1 404 Not Found', ask("GET /nope HTTP/1.1\r\nHost: x\r\n\r\n").first.first
  end

  # RFC 9112 9.3.2: a client may send several requests before it reads a
  # response, and the server answers them in order.
  def test_two_requests_in_one_write_get_two_answers
    assert_equal ['HTTP/1.1 200 OK'] * 2, ask(HELLO * 2).map(&:first)
  end

  # RFC 9112 9.6: a client that closes its write side after the request
  # still reads the response. The server is in CLOSE-WAIT (RFC 9293 3.3.2)
  # and sends the whole answer before it closes.
  def test_a_half_close_after_the_request_still_gets_the_response
    socket = UNIXSocket.new(@path)
    socket.write(HELLO * 4)
    socket.close_write
    sleep 0.3
    answers = answers_of(read_to_end(socket))
    assert_equal ['HTTP/1.1 200 OK'] * 4, answers.map(&:first)
    whole = ask(HELLO).first.last
    answers.each { |answer| assert_equal whole, answer.last }
  ensure
    socket&.close
  end

  def refused_and_closed(bytes)
    socket = UNIXSocket.new(@path)
    socket.write(bytes)
    got = read_to_end(socket)
    [answers_of(got), got]
  ensure
    socket&.close
  end

  # RFC 9112 3.2: a server MUST answer 400 to an HTTP/1.1 request without
  # Host, with two Host field lines, or with an invalid Host. RFC 9112
  # 5.1: whitespace between the field name and the colon is 400. RFC 9112
  # 2.2: after a refusal of the framing, the server closes, and it sends
  # Connection: close before it does (RFC 9112 9.6).
  def test_a_refused_request_gets_400_and_the_connection_closes
    ["GET /hello HTTP/1.1\r\n\r\n",
     "GET /hello HTTP/1.1\r\nHost: x\r\nHost: x\r\n\r\n",
     "GET /hello HTTP/1.1\r\nHost: x y\r\n\r\n",
     "GET /hello HTTP/1.1\r\nHost : x\r\n\r\n",
     "GET /hello HTTP/1.1\r\nHost: x\r\nX: a\r\n b\r\n\r\n",
     "GET /hello HTTP/1.1\r\nHost: x\r\nX: a\rb\r\n\r\n"].each do |bytes|
      answers, got = refused_and_closed(bytes)
      assert_equal ['HTTP/1.1 400 Bad Request'], answers.map(&:first), bytes.inspect
      assert_match(/^Connection: close\r$/i, got, bytes.inspect)
    end
  end

  # RFC 9112 2.2: the bytes after a refused request cannot be trusted to
  # start a request, so the request that follows in the same write gets
  # no answer.
  def test_a_request_after_a_refused_one_gets_no_answer
    answers, = refused_and_closed("GET /hello HTTP/1.1\r\n\r\n#{HELLO}")
    assert_equal ['HTTP/1.1 400 Bad Request'], answers.map(&:first)
  end

  # A request before the refused one is answered first, in order.
  def test_a_request_before_a_refused_one_is_answered
    answers, = refused_and_closed("#{HELLO}GET /hello HTTP/1.1\r\n\r\n#{HELLO}")
    assert_equal ['HTTP/1.1 200 OK', 'HTTP/1.1 400 Bad Request'], answers.map(&:first)
  end

  # RFC 9112 6.3 item 6: the body of a request is Content-Length bytes,
  # and what follows it is the next request. Until this server reads
  # bodies, it answers a request with a body and closes, so the body is
  # never read as a request.
  def test_a_body_is_never_read_as_a_request
    smuggled = HELLO
    answers, = refused_and_closed("POST /hello HTTP/1.1\r\nHost: x\r\nContent-Length: #{smuggled.bytesize}\r\n\r\n#{smuggled}")
    assert_equal 1, answers.size
    answers, = refused_and_closed("GET /hello HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabc#{HELLO}")
    assert_equal ['HTTP/1.1 200 OK'], answers.map(&:first)
  end

  # RFC 9112 6.1: a chunked body is not read here, so the request gets
  # 411 and the bytes after it get no answer.
  def test_a_chunked_request_gets_411_and_the_connection_closes
    answers, = refused_and_closed("POST /hello HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n" \
                                  "#{HELLO.bytesize.to_s(16)}\r\n#{HELLO}\r\n0\r\n\r\n#{HELLO}")
    assert_equal ['HTTP/1.1 411 Length Required'], answers.map(&:first)
  end

  # RFC 9110 15.5.14: a body above the limit of the server gets 413.
  def test_a_body_above_the_limit_gets_413
    answers, = refused_and_closed("POST /hello HTTP/1.1\r\nHost: x\r\nContent-Length: #{(1 << 20) + 1}\r\n\r\n")
    assert_equal ['HTTP/1.1 413 Content Too Large'], answers.map(&:first)
  end

  # RFC 9112 3.2 asks for Host in HTTP/1.1 only.
  def test_an_http_1_0_request_without_host_is_answered
    assert_equal 'HTTP/1.1 200 OK', ask("GET /hello HTTP/1.0\r\n\r\n").first.first
  end

  # RFC 9293 3.10.7.1: a reset ends the connection at once. The server
  # closes the slot and serves the next connection.
  def test_a_reset_from_the_client_leaves_the_server_serving
    socket = UNIXSocket.new(@path)
    socket.write(HELLO * 2)
    socket.close
    sleep 0.1
    3.times do
      socket = Socket.new(:UNIX, :STREAM)
      socket.connect(Socket.sockaddr_un(@path))
      socket.setsockopt(Socket::SOL_SOCKET, Socket::SO_LINGER, [1, 0].pack('ii'))
      socket.write(HELLO)
      socket.close
    end
    assert_equal 'HTTP/1.1 200 OK', ask(HELLO).first.first
  end
end
