#include "PeerManagerTypes.hpp"
#include <cerrno>
#include <cstring>
#include <ev++.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

bool peer_nonblock_tcp::open_socket(int __domain) {
  // cannot set socket when a socket is already assigned
  // A new socket can only be assigned after pclose.
  if (__socket !=-1) {
    perrno = EINVAL;
    return false;
  }
  if (__domain != AF_INET && __domain != AF_INET6) {
    perrno = EAFNOSUPPORT;
    return false;
  }

  int socket_return; do {
    socket_return = socket( __domain, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP );
  } while (socket_return<0 && errno == EINTR);

  if (socket_return < 0) {
    perrno = errno;
    return false;
  }
  __socket = socket_return;
  return true;
}

pconnect_return_t peer_nonblock_tcp::pconnect( const sockaddr* addr,int address_family) {
  if (__socket == -1) {
    perrno = EBADF;
    return failed;
  }
  socklen_t addrlen;
  if      (address_family == AF_INET)   addrlen = sizeof(sockaddr_in);
  else if (address_family == AF_INET6)  addrlen = sizeof(sockaddr_in6);
  else {
    perrno = EAFNOSUPPORT;
    return failed;
  }
  int connect_return; do {
    connect_return = connect(__socket, addr, addrlen);
  } while (connect_return < 0 && errno == EINTR);
  if (connect_return == 0) {
    perrno = -1;
    return connected;
  }
  if (connect_return < 0 && errno == EINPROGRESS) {
    perrno = EINPROGRESS;
    return inprogress;
  }
  perrno = errno;
  return failed;
}

bool peer_nonblock_tcp::handle_send_perrno(int err) {
  perrno = err;
  switch (perrno) {
    case EAGAIN:
      return true;
    case EPIPE:
    case ECONNRESET:
    default:
      return false;
  }
}

bool peer_nonblock_tcp::handle_recv_perrno(int err) {
  perrno = err;
  switch (perrno) {
    case EAGAIN:
      return true;
    default:
      return false;
  }
}

bool peer_nonblock_tcp::handle_connect_perrno(int err) {
  perrno = err;
  switch (perrno) {
    case EINPROGRESS:
      return true;
    default:
      return false;
  }
}

void peer_nonblock_tcp::close_socket() {
  if (__socket != -1) {
    close(__socket);
    __socket = -1;
  }
  perrno = -1;
  std::memset(&ephemereal_hdr, 0, sizeof(ephemereal_hdr));
}

void peer_nonblock_tcp::disconnect(){
  close_socket();
}

int peer_nonblock_tcp::get_socket() {
  return __socket;
}

[[ nodiscard ]] bool peer_nonblock_tcp::adopt_socket(int __sock) {
  if (__socket!=-1 || __sock ==-1)
    return false;
  __socket = __sock;
  return false;
}

int peer_nonblock_tcp::get_errno() {
  return perrno;
}

peer_nonblock_tcp::~peer_nonblock_tcp() noexcept {
  close_socket();
}


peer_nonblock_tcp::peer_nonblock_tcp(peer_nonblock_tcp&& other) noexcept {
 __socket = other.__socket;
 perrno = other.perrno;
 ephemereal_hdr.msg_iov = other.ephemereal_hdr.msg_iov;
 ephemereal_hdr.msg_iovlen = other.ephemereal_hdr.msg_iovlen;
 other.close_socket();
}

peer_nonblock_tcp& peer_nonblock_tcp::operator=(peer_nonblock_tcp&& other) noexcept {
  if (this == &other)
    return *this;

  close_socket();

  __socket = other.__socket;
  perrno = other.perrno;
  ephemereal_hdr.msg_iov = other.ephemereal_hdr.msg_iov;
  ephemereal_hdr.msg_iovlen = other.ephemereal_hdr.msg_iovlen;
  other.close_socket();

  return *this;
}

void peer_watchers::stop() {
  for_sock.stop();
  for_timer.stop();
}

send_transact PeerConnection::send_messages() {
  return tcp.send(send_buffer);
}

recv_transact PeerConnection::recv_messages() {
  return tcp.recv(recv_buffer);
}

PeerConnection PeerSession::dummypeer{};

send_transact PeerSession::send_messages() {
  return tcp.send(send_buffer);
}

recv_transact PeerSession::recv_messages() {
  return tcp.recv(recv_buffer);
}

bool PeerSession::is_dummy() {
  return peer == &dummypeer;
}

void PeerSession::set_endpoint(const connect_update endpoint) {
  peer = endpoint.peer;
  id = endpoint.id;
  generation = endpoint.generation;
  (void)tcp.adopt_socket(endpoint.socket);
}

disconnect_update PeerSession::endpoint_disconnected() {
  disconnect_update disconnected { .peer=peer, .generation=generation, .perrno=tcp.get_errno() };
  id         = 0;
  generation = 0;
  down_rate  = 0;
  upld_rate  = 0;
  choke.set();
  interest.reset();
  bitfield.clear();
  tcp.close_socket();
  recv_buffer.reset();
  send_buffer.reset();
  watcher.for_sock.stop();
  watcher.for_timer.stop();
  peer = &dummypeer;
  return disconnected;
}

void peer_stats_t::reset() {
  failures = 0;
}
