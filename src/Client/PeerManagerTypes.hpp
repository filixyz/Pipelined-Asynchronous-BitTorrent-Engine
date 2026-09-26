#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <unistd.h>
#include <bitset>
#include <ev++.h>
#include "DynamicBitset.hpp"
#include "io_ring_buffer.hpp"
#include "ThreadMessageTypes.hpp"
#include "bittorrent_messages.hpp"
#include "Constants.hpp"
#include "Hasher.hpp"
//Unix Networking Headers here
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <cerrno>
#include <arpa/inet.h>
#include <netdb.h>
inline constexpr int PEER_SHUTDOWN = -100;
inline constexpr int NO_ERROR      = -1;

class  PeerTransferManager;
class  PeerConnectionManager;
struct PeerConnection;
struct PeerSession;
struct peer_nonblock_tcp;

struct send_transact {
  bool transport_ok;
  bool buffer_empty;          // THis tells caller that something was removed from buffer
  std::size_t sent_bytes;    // THis is the size of the something
};

struct recv_transact {
  bool transport_ok;
  bool buffer_full;           // This tells caller that something was added to buffer
  std::size_t recvd_bytes;     // This is the size of the something
};

enum pconnect_return_t:std::uint8_t { inprogress, failed, connected };

struct peer_nonblock_tcp {
private:
  int __socket{-1};
  int perrno{-1};
  msghdr ephemereal_hdr{};
public:
  template<std::size_t N>
  send_transact send(io_ring_buffer<N>& buffer) {
    prepare_t prepare = buffer.prepare_read();
    ephemereal_hdr.msg_iov = prepare.iovec_array.data();
    ephemereal_hdr.msg_iovlen = prepare.prepared_iovecs;

    ssize_t send_return; do {
      send_return = sendmsg(__socket, &ephemereal_hdr, MSG_NOSIGNAL);
    } while (send_return<0 && errno == EINTR);

    if (send_return<0)
      return {handle_send_perrno(errno), buffer.empty(), 0};
    perrno = -1;
    buffer.commit_read(send_return);
    return {true, buffer.empty(), static_cast<std::size_t>(send_return)};
  }

  template<std::size_t N>
  recv_transact recv(io_ring_buffer<N>& buffer) {
    prepare_t prepare = buffer.prepare_write();
    ephemereal_hdr.msg_iov = prepare.iovec_array.data();
    ephemereal_hdr.msg_iovlen = prepare.prepared_iovecs;

    ssize_t recv_return; do {
      recv_return = recvmsg(__socket, &ephemereal_hdr, 0);
    } while (recv_return<0 && errno == EINTR);

    if (recv_return == 0) {
      perrno = PEER_SHUTDOWN;
      return {false, buffer.full(), 0};
    } else if (recv_return<0) {
      return {handle_recv_perrno(errno), buffer.full(), 0};
    }
    perrno = -1;
    buffer.commit_write(recv_return);
    return {true, buffer.full(), static_cast<std::size_t>(recv_return)};
  }
  [[nodiscard]] bool adopt_socket(int);
  int  get_socket();
  int  get_errno();
  void close_socket();
public:
  peer_nonblock_tcp() = default;
  peer_nonblock_tcp(const peer_nonblock_tcp&) = delete;
  peer_nonblock_tcp& operator=(const peer_nonblock_tcp&) = delete;
  ~peer_nonblock_tcp() noexcept;
  peer_nonblock_tcp(peer_nonblock_tcp&& other) noexcept;
  peer_nonblock_tcp& operator=(peer_nonblock_tcp&& other) noexcept;
private:
  bool handle_send_perrno(int);
  bool handle_recv_perrno(int);
  bool handle_connect_perrno(int);
  pconnect_return_t pconnect(const sockaddr*, int);
  bool open_socket(int __domain);
  void disconnect();
  friend PeerConnectionManager;
};

enum class pstate:  std::uint8_t  {null, DISCOVERED, HANDSHAKE, CONNECTED, DISCONNECTED, FAILED};
enum class psource: std::uint8_t  {null, tracker, tcp_server};
enum class pipv:    std::uint8_t  {null, ipv4, ipv6, ipv4maskedv6};
using peer_id_t                =  std::array<std::byte, 20>;
union peer_key_t                  { ipv4_peer_address ipv4; ipv6_peer_address ipv6; };
union peer_sock_store_t           { sockaddr_in ipv4_store; sockaddr_in6 ipv6_store; };

struct peer_stats_t{
  std::size_t failures{0};
  void reset();
};

struct peer_watchers {
  ev::io for_sock;
  ev::timer for_timer;
  void stop();
};

struct tranport_frame_cursors {
  // current message_type
  bittorrent_messages::frame_cursor incoming;
};


using hanshake_buffer   = io_ring_buffer<68>;
using session_buffer    = io_ring_buffer< uint8_t(1)<<bprotocol::constants::tcp_bufexp >;
using timer_clbk_t      = void (*) (ev::timer&, int);
using sock_clbk_t       = void (*) (ev::io&, int);

struct PeerConnection {

  peer_nonblock_tcp tcp;
  peer_sock_store_t store{};
  hanshake_buffer recv_buffer{};
  hanshake_buffer send_buffer{};
  peer_watchers listener;

  peer_key_t key {};
  peer_id_t peer_id {};
  pstate state {pstate::null};
  psource source {psource::null};
  pipv IPv {pipv::null};
  bittorrent_messages::frame_cursor outgoing_frame_cursor;
  peer_stats_t stats;

  template <sock_clbk_t socket_callback, timer_clbk_t timer_callback>
  void initialize_connection (peer_key_t&, pipv, psource, pstate, ev::dynamic_loop&);
  void teardown_connection();

  recv_transact recv_messages();
  send_transact send_messages();
};

struct connect_update {
  const PeerConnection* peer;
  int socket;
  std::size_t id;
  std::size_t generation;
};

struct disconnect_update {
  const PeerConnection* peer;
  std::size_t generation;
  int perrno;
};

struct PeerSession {
  enum from { me=0, them=1 };
public:
  peer_nonblock_tcp tcp;
  session_buffer recv_buffer{};
  session_buffer send_buffer{};
  peer_watchers watcher;
  std::size_t id;
  std::size_t generation;
  std::bitset<2> choke {};
  std::bitset<2> interest {};
  std::size_t down_rate{0};
  std::size_t upld_rate{0};
  DynamicBitset bitfield;

  bool is_dummy();
  void set_endpoint(const connect_update);
  disconnect_update endpoint_disconnected();
  send_transact send_messages();
  recv_transact recv_messages();
private:
  const  PeerConnection* peer {&dummypeer};
  static PeerConnection  dummypeer;
};

using pconnection_queue = beamable_spsc_t<connect_update, 50>;
using pdisconnection_queue = beamable_spsc_t<disconnect_update, 50>;

struct peer_manager_hashers {
  std::uint64_t operator()(const ipv4_peer_address& key) const noexcept {
    return Hasher::fnv_1a_64bits(key.iport);
  }
  std::uint64_t operator()(const ipv6_peer_address& key) const noexcept {
    return Hasher::fnv_1a_64bits(key.iport);
  }
  std::uint64_t operator()(const peer_id_t& key) const noexcept {
    return Hasher::fnv_1a_64bits(key);
  }
};

struct peer_id_gen {
  std::size_t id{0};
  std::size_t operator()() { return id++; }
};
