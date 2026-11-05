C = Webmachine::SpecConnection

# RFC 9293 3.3.2 State Machine Overview
# These are the moves of a TCP connection that a server process can see
# after an accept. A recv that gives 0 means that the peer sent FIN, so
# the connection is in CLOSE-WAIT. A close or a shutdown of the write side
# sends FIN: from ESTABLISHED to FIN-WAIT-1, from CLOSE-WAIT to LAST-ACK.
# A reset ends every state in CLOSED. FIN-WAIT-2, CLOSING and TIME-WAIT
# are moves of the kernel that the process cannot see.
TCP_MOVES = {
  closed: [],
  established: [:close_wait, :fin_wait_1, :closed],
  fin_wait_1: [:closed],
  close_wait: [:last_ack, :closed],
  last_ack: [:closed]
}

assert('every TCP state of RFC 9293 that a server can see has a name') do
  assert_equal TCP_MOVES.keys.sort, C.tcp_states.sort
end

assert('a TCP state moves only where RFC 9293 3.3.2 moves it') do
  TCP_MOVES.each_key do |from|
    TCP_MOVES.each_key do |to|
      assert_equal TCP_MOVES[from].include?(to), C.tcp_may_move?(from, to), "#{from} to #{to}"
      assert_equal TCP_MOVES[from].include?(to), C.tcp_moves?(from, to), "#{from} to #{to}"
    end
  end
end

# RFC 9293 3.10.4 CLOSE call, RFC 9293 3.10.7.1: a reset or a write to a
# connection that the peer reset ends it at once. RFC 9293 3.10.8: a user
# timeout ends it at once too. A close of the server
# otherwise sends FIN, and the state depends on whether the FIN of the
# peer came first.
assert('a close moves ESTABLISHED to FIN-WAIT-1 and CLOSE-WAIT to LAST-ACK') do
  assert_equal :fin_wait_1, C.state_after_close(:established, 0)
  assert_equal :last_ack, C.state_after_close(:close_wait, 0)
  assert_equal :fin_wait_1, C.state_after_close(:established, -C.errno(:EIO))
  assert_equal :fin_wait_1, C.state_after_close(:established, -C.errno(:ENOMEM))
end

assert('a reset, a broken pipe or a user timeout moves to CLOSED') do
  [:established, :close_wait].each do |now|
    assert_equal :closed, C.state_after_close(now, -C.errno(:ECONNRESET))
    assert_equal :closed, C.state_after_close(now, -C.errno(:EPIPE))
    assert_equal :closed, C.state_after_close(now, -C.errno(:ETIMEDOUT))
  end
end

# RFC 9110 6.3 Header Fields, RFC 9112 6 Message Body, RFC 9112 9.3
# Persistence, RFC 9110 15.5 Client Error, RFC 9112 9.6 Tear-down.
# A request is read as a header section, then a body where one follows,
# and then it is dispatched to a resource. No state goes back from the
# body to the header section. A persistent connection reads the next
# header section; a client error and a tear-down end the connection.
HTTP_MOVES = {
  header_section: [:message_body, :dispatch, :client_error],
  message_body: [:dispatch, :client_error],
  dispatch: [:suspended, :response],
  suspended: [:response],
  response: [:persistent, :tear_down],
  persistent: [:header_section, :tear_down],
  client_error: [:tear_down],
  tear_down: []
}

assert('every HTTP state of a connection has a name') do
  assert_equal HTTP_MOVES.keys.sort, C.http_states.sort
end

assert('an HTTP state moves only where the plan of RFC 9112 moves it') do
  HTTP_MOVES.each_key do |from|
    HTTP_MOVES.each_key do |to|
      assert_equal HTTP_MOVES[from].include?(to), C.http_may_move?(from, to), "#{from} to #{to}"
      assert_equal HTTP_MOVES[from].include?(to), C.http_moves?(from, to), "#{from} to #{to}"
    end
  end
end

# A file index is used again after its close. The generation tells the
# completions of the old connection from those of the new one.
assert('an accept starts in ESTABLISHED and header_section with the next generation') do
  assert_equal [:established, :header_section, 8], C.accepted_after(7)
  assert_equal [:established, :header_section, 0], C.accepted_after(4294967295)
end

# The user data of a completion holds what was done, the file index and the
# generation, and each comes back whole at the edges of its width.
assert('a mark gives back what was done, the file index and the generation') do
  most = C.file_indices_most
  assert_equal 1 << 20, most
  [[1, 0, 0], [2, most - 1, 4294967295], [4, 12345, 1], [3, 1, 2147483648]].each do |doing, file_index, generation|
    assert_equal [doing, file_index, generation], C.mark(doing, file_index, generation)
  end
end
