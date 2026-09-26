#include "Constants.hpp"
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <ev++.h>
#include <ev.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../Errorhandlers/BittorentErrors.hpp"
#include "PeerConnectionManager.hpp"
#include "PeerManagerTypes.hpp"
#include "ThreadMessageTypes.hpp"
#include "bittorrent_messages.hpp"

void PeerConnectionManager::ipv6_default_server_sockstore() {
  auto& server = outbound_connection_server.parameters;
  std::memset(&server.store, 0, sizeof(sockaddr_in));
  server.store.ipv6.sin6_family = AF_INET6;
  server.store_len = sizeof(sockaddr_in6);
}

void PeerConnectionManager::ipv4_default_server_sockstore() {
  auto& server = outbound_connection_server.parameters;
  std::memset(&server.store, 0, sizeof(sockaddr_in6));
  server.store.ipv4.sin_family = AF_INET;
  server.store_len = sizeof(sockaddr_in);
}

void PeerConnectionManager::initialize_manager_watchers() {
  auto& server = outbound_connection_server;
  server.watcher.set(event_loop);
  server.watcher.set(server.parameters.socket, ev::READ);
  server.watcher.set<PeerConnectionManager, &PeerConnectionManager::server_socket_callback>(this);
  discoveries.beamable_spsc.consumer.set(event_loop);
  discoveries.beamable_spsc.consumer.set<PeerConnectionManager, &PeerConnectionManager::drain_discovered>(this);
  disconnects.consumer.set(event_loop);
  disconnects.consumer.set<PeerConnectionManager, &PeerConnectionManager::notify_disconnected>(this);
}

bittorrent_messages::handshake_t PeerConnectionManager::compute_handshake() {
  bittorrent_messages::handshake_t handshake {};
  std::size_t offset = 0;
  std::memcpy(&handshake[offset], &bittorrent_messages::protocol_string_length, 1);     offset +=  1;
  std::memcpy(&handshake[offset], &bittorrent_messages::protocol_string, 19);           offset += 19;
  std::memcpy(&handshake[offset], bprotocol::constants::reserved_bytes.data(), 8);      offset +=  8;
  std::memcpy(&handshake[offset], torrent.get_info_hash_bytes().data(), 20);            offset += 20;
  std::memcpy(&handshake[offset], bprotocol::constants::client_id.data(), 20);          offset += 20;
  return handshake;
};

void PeerConnectionManager::drain_discovered() {
  ipv4_peer_address addr;
  while (discoveries.beamable_spsc.queue.pop(addr)) {
    discoveries.cache.push(std::move(addr));
    statistics.increment_ipv4_addr_in_cache();
  }
  if (statistics.has_met_connection_quota() or !connection_pool.available())
    return;
  inbound_connection_scheduler.send_notification();
}

void PeerConnectionManager::notify_disconnected() {
  inbound_connection_scheduler.send_notification();
}

void PeerConnectionManager::deregister_from_map(PeerConnection& peer) {
  if (peer.IPv == pipv::ipv6)
    ipv6_peers.erase(peer.key.ipv6);
  else if (peer.IPv == pipv::ipv4 || peer.IPv == pipv::ipv4maskedv6)
    ipv4_peers.erase(peer.key.ipv4);
  else
   assert(false && "Peer with no ipv found in connection_manager erase function");
}

// initates tcp connect() on peer, alloactes tcp fd and connect() to it
// upon failure on allocating socket it returns, upon failure on connect() to socket it closes the fd
// on success sets listeners for peer, handles immediate connect scenario (maybe peer is on the same host
// different port) and returns true
bool PeerConnectionManager::connect(PeerConnection& peer) {

  assert (peer.source != psource::tracker);

  sockaddr* sock_addr = reinterpret_cast<sockaddr*>(&peer.store);

  if ( peer.tcp.open_socket(sock_addr->sa_family) == false )
    return false;

  pconnect_return_t resolve = peer.tcp.pconnect(sock_addr, sock_addr->sa_family);

  if ( resolve == failed ) {
    peer.tcp.close_socket();
    return false;
  }

  arm_connection_watchers(peer, EV_WRITE);

  if ( resolve == connected ) {
    peer.stats             .reset();
    peer.state =            pstate::HANDSHAKE;
    peer.listener.for_sock .feed_event(EV_WRITE);
  }

  if ( resolve == inprogress ) { /**  do nothing **/ }

  return true;
}

// NOTE TO SELF: delete this function and inline it where it's called.
// upon successful trsnport level connect() inititaion number of peers in flight is incremented
// true returned; otherwise false
bool inbound_scheduler_t::initiate_connect(PeerConnection& peer) {

  assert(peer.source == psource::tracker);
  if (manager.connect(peer)) {
    manager.statistics.increment_inbound_inflight();
    return true;
  }

  return false;
}

// establisher: Handlers should never close sockets.
// manager::connect() closes socket upon notice of immediate failure
// manager::handle_failure() also closes socket upon transient notice of peer transport failure
// tranfermanger also close socket of peers relayed to it upon it's immediate notice of peer transport failure

bool inbound_scheduler_t::discovered_peer_scheduler() {

  // guard connection pool from overgrowing
  // larger than it's boundary.
  if (manager.connection_pool.available() == 0)
    return false;

  ipv4_peer_address addr;
  while ( manager.discoveries.cache.fresh_pop(addr)==true ) {
    manager.statistics.decrement_ipv4_addr_in_cache();
    auto [acquisition_successful,  acquired_slot] = manager.connection_pool.acquire();
    assert(acquisition_successful);
    auto [it, inserted] = manager.ipv4_peers.try_emplace(addr, acquired_slot);
    if ( inserted == false ) {
      manager.connection_pool.release(acquired_slot);
      continue;
    }
    peer_key_t peer_addr{ .ipv4=addr };
    auto& peer = acquired_slot->object;
    peer.initialize_connection
      <PeerConnectionManager::peer_socket_callback, PeerConnectionManager::peer_timer_callback> (
        peer_addr, pipv::ipv4, psource::tracker, pstate::DISCOVERED, manager.event_loop
    );
    if ( initiate_connect(peer) == false ) {
      manager.delete_peer_connection(peer);
      continue;
    }
    return true;
  }
  return false;
}

bool inbound_scheduler_t::disconnected_peer_scheduler() {
  disconnect_update disconnected;
  while ( manager.disconnects.queue.pop(disconnected)==true ) {
    auto& peer = * const_cast<PeerConnection*>(disconnected.peer);
    connection_slot* peer_slot = reinterpret_cast<connection_slot*>(&peer);

    if (peer_slot->generation() != disconnected.generation) {
      continue;
    }
    peer.state = pstate::DISCONNECTED;
    manager.statistics.decrement_connected_bittorrent_peers();
    if (peer.tcp.get_errno() == PEER_SHUTDOWN || peer.source == psource::tcp_server) {
      manager.delete_peer_connection(peer);
      continue;
    }
    peer.stats.failures++;
    if ( initiate_connect(peer) == false ) {
      manager.delete_peer_connection(peer);
      continue;
    }
    return true;
  }
  return false;
}

// peer can only reach her if handled with manager.handle_peer_failure()
bool inbound_scheduler_t::failed_peer_scheduler() {

  while ( manager.retry_queue.empty() == false ) {
    peer_failure_update failed_peer = manager.retry_queue.front();
    manager.retry_queue.pop();
    manager.statistics.decrement_peers_in_retry();
    if (failed_peer.connection->generation() != failed_peer.cached_generation)
      continue;
    PeerConnection& peer = failed_peer.connection->object;
    if ( initiate_connect(peer) == false ) {
      manager.delete_peer_connection(peer);
      continue;
    }
    return true;
  }
  return false;

}

void inbound_scheduler_t::plus_mask_current(std::size_t spot) {
  current = (spot+1 == handlers_count) ? static_cast<spot_t>(0) : static_cast<spot_t>(spot+1);
}

void inbound_scheduler_t::round_robin_establisher_scheduler() {
  // This is a load balancer.
  for (; manager.statistics.get_inbound_inflight() < bprotocol::constants::max_inbound_inflight; ) {

    std::size_t spot = static_cast<std::size_t>(current);

    if (current == discovered)
      empties[spot] = !discovered_peer_scheduler();
    else if (current == disconnected)
      empties[spot] = !disconnected_peer_scheduler();
    else if (current == failed)
      empties[spot] = !failed_peer_scheduler();

    if (empties[0] && empties[1] && empties[2])
      break;

    plus_mask_current(spot);

  }
}

inbound_scheduler_t::inbound_scheduler_t(PeerConnectionManager& __manager): manager(__manager) {
  daemon.set<inbound_scheduler_t, &inbound_scheduler_t::round_robin_establisher_scheduler>(this);
  daemon.set(manager.event_loop);
}

void PeerConnectionManager::initialize_server_socket() {
  // create socket
  auto& server = outbound_connection_server.parameters;
  sockaddr* sock_addr = reinterpret_cast<sockaddr*>( &server.store );
  ipv6_default_server_sockstore();
  server.socket = socket(AF_INET6, server.flags, server.trspt_proto);
  if (server.socket<0)
    handle_socket_errno(errno);
  // put off ipv6 only
  if (sock_addr->sa_family == AF_INET6) {
    int ipv6only_off_return = setsockopt(server.socket, IPPROTO_IPV6, IPV6_V6ONLY, &server.off_ipv6only, sizeof(server.off_ipv6only));
    if (ipv6only_off_return == 0)
      server.ipv4_support = true;
    else
      handle_ip_errno(errno);
  }
  // bind socket
  if (sock_addr->sa_family == AF_INET6) {
    server.store.ipv6.sin6_addr = in6addr_any;
    server.store.ipv6.sin6_port = 0;
  } else {
    server.store.ipv4.sin_addr.s_addr = INADDR_ANY;
    server.store.ipv4.sin_port = 0;
  }
  while (true) {
    int bind_return = bind(server.socket, sock_addr , server.store_len);
    if (bind_return == 0) break;
    handle_bind_errno(errno);
  }
  // get listening port
  int get_sock_name_return = getsockname(server.socket, sock_addr , &server.store_len);
  if (get_sock_name_return != 0)
    throw Peer_Manager_SYS_Error{errno};
  server.port = ntohs( sock_addr->sa_family ==AF_INET6 ? server.store.ipv6.sin6_port : server.store.ipv4.sin_port);
  // mark as listening
  int listen_return = listen(server.socket, bprotocol::constants::connection_backlog);
  if (listen_return != 0)
    throw Peer_Manager_SYS_Error{errno};
}

int PeerConnectionManager::initialize_libev() {
  return ev::recommended_backends();
}

PeerConnectionManager::PeerConnectionManager(TorrentFile& a, pconnection_queue& b)
  :event_loop(initialize_libev()), statistics(event_loop, tick_duration), inbound_connection_scheduler(*this),
   torrent(a), handshake(compute_handshake()), connects(b)
{
  ev_set_userdata(event_loop.raw_loop, this);
  initialize_server_socket();
  initialize_manager_watchers();
}

bool PeerConnectionManager::accept_peer_connection() {

  peer_sock_store_t new_store{};
  sockaddr* sock_addr = reinterpret_cast<sockaddr*>(&new_store);
  auto& server = outbound_connection_server.parameters;

  int accept_return = accept4(server.socket, (sockaddr*)&new_store, &server.store_len, SOCK_NONBLOCK);
  if (accept_return<0)
    return handle_server_errno(errno);

  // protect agains overflowing to a socket count
  // that connection_pool cannot allocate peers for.
  if (connection_pool.available() == 0) {
    close(accept_return);
    return false;
  }

  statistics.increment_outbound_inflight();

  // extract peer id
  pipv ip_version = pipv::null;
  peer_key_t peer_addr {};

  if (sock_addr->sa_family ==AF_INET6) {
    if (IN6_IS_ADDR_V4MAPPED(&new_store.ipv6_store.sin6_addr)) {
      ip_version = pipv::ipv4maskedv6;
      std::memcpy(&peer_addr.ipv4, &new_store.ipv6_store.sin6_addr.s6_addr[12], sizeof(in_addr) );
      std::memcpy(&peer_addr.ipv4.iport[4], &new_store.ipv4_store.sin_port, sizeof(in_port_t));
    } else {
      ip_version = pipv::ipv6;
      std::memcpy(&peer_addr.ipv6, &new_store.ipv6_store.sin6_addr, sizeof(in6_addr) );
      std::memcpy(&peer_addr.ipv6.iport[16], &new_store.ipv6_store.sin6_port, sizeof(in_port_t));
    }
  }
  else if (sock_addr->sa_family==AF_INET) {
    ip_version = pipv::ipv4;
    std::memcpy(&peer_addr.ipv4, &new_store.ipv4_store.sin_addr, sizeof(in_addr) );
    std::memcpy(&peer_addr.ipv4.iport[4], &new_store.ipv4_store.sin_port, sizeof(in_port_t));
  }
  else {
    assert(false && "Unexpected Address Family: accept_peer_connection");
  }

  auto [acquisition_sucessful, acquired_slot] = connection_pool.acquire();
  assert (acquisition_sucessful);

  // now to check is peer endpoint is currently being managed
  // by peer manager, maybe it was a server tcp peer and for whatever
  // reason switched to being a client peer that wants to connect to my
  // implementations server endpoint
  //
  // The reasoning is that for tcp to hand me this peer and it's currently
  // still stored in my peer table, it must mean peers previous session is
  // currently disconnected and either my client has not yet noticed it or
  // it currently in the process of trying to connect to it
  //
  // but one thing is for sure for the enpoint to reach hear i have sucessfully
  // established a tcp connection with it via my server endpoint
  // what to do about this then?.

  bool unique = false;
  connection_slot* mapped_connection_slot;
  if (ip_version == pipv::ipv4 || ip_version == pipv::ipv4maskedv6) {
    auto [it, inserted] = ipv4_peers.try_emplace(peer_addr.ipv4, acquired_slot);
    unique = inserted;
    mapped_connection_slot = it->second;
  } else if (ip_version == pipv::ipv6) {
    auto [it, inserted] = ipv6_peers.try_emplace(peer_addr.ipv6, acquired_slot);
    mapped_connection_slot = it->second;
  } else
    assert(false && "failsafe, something wrong in accept_peer_connection");


  if (unique == false) {

    // auto& old_peer = mapped_connection_slot->object;
    // // This guards against inflight peers
    // // and stops the connection establishments
    // stop_connection_watchers(old_peer);
    // // THis guards against closing an fd that is being managed
    // // by transfermanager since it's tranfermanager duty to close sockets
    // // it finds faulty, if not connected this should mean peer is mostlikely being
    // // retried or in the process of a dead establishment
    // if (old_peer.state != pstate::CONNECTED)
    //   old_peer.tcp.close_socket();
    // old_peer.teardown_connection();

    // // this invalidates failed peers and disconnected peers caches
    // connection_pool.release(mapped_connection_slot);

    auto& old_peer = mapped_connection_slot->object;
    if (old_peer.state != pstate::CONNECTED) {

      old_peer.tcp.close_socket();
      if (old_peer.source == psource::tcp_server)
        statistics.single_outbound_resolved();
      else {
        statistics.single_inbound_resolved();
        inbound_connection_scheduler.send_notification();
      }
    }

    delete_peer_connection(old_peer);

    if  (ip_version == pipv::ipv6)
      ipv6_peers[peer_addr.ipv6] = acquired_slot;
    else
      ipv4_peers[peer_addr.ipv4] = acquired_slot;
  }

  PeerConnection& peer = acquired_slot->object;

  peer.initialize_connection
    <PeerConnectionManager::peer_socket_callback, PeerConnectionManager::peer_timer_callback> (
      peer_addr, ip_version, psource::tcp_server, pstate::HANDSHAKE, event_loop
  );
  initialize_server_specifics(peer, accept_return, &new_store);
  arm_connection_watchers(peer, EV_READ);

  return true;
}

void PeerConnectionManager::handle_peer_failure(PeerConnection& peer) {
  peer.state = pstate::FAILED;
  peer.stats.failures++;

  assert(peer.source != psource::null);

  peer.tcp.close_socket();
  if (peer.source == psource::tcp_server)
    statistics.single_outbound_resolved();
  else {
    statistics.single_inbound_resolved();
    inbound_connection_scheduler.send_notification();
  }

  if (peer.stats.failures >= bprotocol::constants::peer::max_reties || peer.source == psource::tcp_server) {
    delete_peer_connection(peer);
    return;
  }

  stop_connection_watchers(peer);
  peer.outgoing_frame_cursor.reset();
  peer.recv_buffer.reset();
  peer.send_buffer.reset();
  peer.listener.for_timer.set( bprotocol::constants::peer::retry_timeout * peer.stats.failures );
  peer.listener.for_timer.start();
  statistics.increment_failed();
}

void PeerConnectionManager::peer_timer_callback(ev::timer& timer, int) {
  PeerConnection& peer = * static_cast<PeerConnection*>(timer.data);
  auto& manager = * static_cast<PeerConnectionManager*> (ev_userdata(timer.loop.raw_loop));
  connection_slot* peer_slot = reinterpret_cast<connection_slot*>(&peer);

  assert(peer.state != pstate::CONNECTED);

  if (peer.state == pstate::FAILED) {
    manager.stop_connection_watchers(peer);
    peer_failure_update new_failure { peer_slot, peer_slot->generation() };
    manager.retry_queue.push(new_failure);
    manager.statistics.decrement_failed();
    manager.statistics.increment_peers_in_retry();
    return;
  }
  // should coalesce to this when in another state, DISCOVERED, HANDSHAKE and DISCONNECTED.
  manager.handle_peer_failure(peer);
}

void PeerConnectionManager::delete_peer_connection(PeerConnection& peer) {
  // teardown_connection stops watchers
  deregister_from_map(peer);
  peer.teardown_connection();
  connection_pool.release(reinterpret_cast<connection_slot*>( &peer ));
}

bool PeerConnectionManager::peer_transport_level_connected(PeerConnection& peer) {
  int error;
  socklen_t err_var_len = sizeof error;

  int sock_opt_return = getsockopt(peer.tcp.get_socket(), SOL_SOCKET, SO_ERROR, &error, &err_var_len);

  if (sock_opt_return<0)
    assert(false && "getsockopt failed");
  else if (error != 0) {
    return false;
  }
  return true;
}

void PeerConnectionManager::handle_peer_application_level_handshake(PeerConnection& peer, int event) {

  // handle partial handshake sends
  if (event & EV_WRITE) {
    auto [transport_ok, buffer_exhausted, sent_bytes] = peer.send_messages();
    if (transport_ok == false) {
      handle_peer_failure(peer);
      return;
    }
    if (buffer_exhausted) {
      if (peer.source == psource::tracker) {
        modify_peer_socket_w_event(peer, ev::READ);
      }
      if (peer.source == psource::tcp_server) {
        handle_peer_connection_and_dispatch(peer);
      }
    }
    return;
  }

  // recieve handshake from connected peer
  if (event & EV_READ) {

    auto [transport_ok, buffer_full, recvd_bytes] = peer.recv_messages();

    if ( !transport_ok ) {
      handle_peer_failure(peer);
      return;
    }

    auto handshake_decode = bittorrent_messages::handshake::decode (
      peer.recv_buffer, torrent.get_info_hash_bytes(), peer.peer_id
    );

    if ( !handshake_decode.complete)
      return;

    if ( !handshake_decode.valid ) {
      peer.tcp.close_socket();
      delete_peer_connection(peer);
      return;
    }

    if (peer.source == psource::tracker)
      handle_peer_connection_and_dispatch(peer);

    if (peer.source == psource::tcp_server) {

      assert(peer.send_buffer.empty());
      peer.outgoing_frame_cursor.reset();
      auto [encode_completed] =
        bittorrent_messages::handshake::encode(handshake, peer.send_buffer, peer.outgoing_frame_cursor);
      auto [transport_ok, buffer_exhausted, sent_bytes] = peer.send_messages();
      assert ( encode_completed );

      if ( !transport_ok) {
        handle_peer_failure(peer);
        return;
      }
      if ( !buffer_exhausted ) {
        modify_peer_socket_w_event(peer, ev::WRITE);
        return;
      }
      handle_peer_connection_and_dispatch(peer);
    }

  }
}

void PeerConnectionManager::handle_peer_transport_level_initiations(PeerConnection& peer, int event) {
  if (event & ev::WRITE)
  {
    if ( !peer_transport_level_connected(peer)) {
      handle_peer_failure(peer);
      return;
    }

    peer.state = pstate::HANDSHAKE;
    assert( peer.send_buffer.empty() );
    peer.outgoing_frame_cursor.reset();
    auto [encode_completed] = bittorrent_messages::handshake::encode( handshake, peer.send_buffer, peer.outgoing_frame_cursor );
    assert( encode_completed );

    auto [transport_ok, buffer_exhausted, sent_bytes] = peer.send_messages();

    if ( transport_ok ) {
      int event = buffer_exhausted ? ev::READ : ev::WRITE;
      modify_peer_socket_w_event(peer, event);
    } else {
      handle_peer_failure(peer);
    }
    return;
  }

  if (event & ev::READ) {
    // not needed. for transport level initiations
  }
}

void PeerConnectionManager::peer_socket_callback(ev::io& sw, int event) {
  auto& peer =
    * static_cast<PeerConnection*> (sw.data);
  auto& manager =
    * static_cast<PeerConnectionManager*> (ev_userdata(sw.loop.raw_loop));

  assert(peer.state != pstate::CONNECTED);

  if (manager.statistics.has_met_connection_quota()) {
    manager.delete_peer_connection(peer);                                    return;
  }

  if (event & ev::ERROR) {
    manager.handle_peer_failure(peer);                                       return;
  }

  switch (peer.state) {
    case pstate::DISCOVERED: case pstate::DISCONNECTED: case pstate::FAILED:
      manager.handle_peer_transport_level_initiations(peer, event);          return;
    case pstate::HANDSHAKE:
      manager.handle_peer_application_level_handshake(peer, event);          return;
    case pstate::null: default:
      manager.delete_peer_connection(peer);                                  return;
  }
}

void PeerConnectionManager::initialize_server_specifics(PeerConnection& peer, int socket, peer_sock_store_t* store) {
  peer.tcp.__socket = socket;
  memcpy(&peer.store, store, outbound_connection_server.parameters.store_len);
}

void PeerConnectionManager::arm_connection_watchers(PeerConnection& peer, int event) {
  modify_peer_socket_w_event(peer, event);
  peer.listener.for_timer.stop();
  peer.listener.for_timer.set(bprotocol::constants::peer::connect_timeout);
  peer.listener.for_timer.start();
}

void PeerConnectionManager::modify_peer_socket_w_event(PeerConnection& peer, int event) {
  peer.listener.for_sock.stop();
  peer.listener.for_sock.set(peer.tcp.get_socket(), event);
  peer.listener.for_sock.start();
}

void PeerConnectionManager::stop_connection_watchers(PeerConnection& peer) {
  peer.listener.for_sock.stop();
  peer.listener.for_timer.stop();
}

void PeerConnectionManager::handle_peer_connection_and_dispatch(PeerConnection& peer) {
  peer.state = pstate::CONNECTED;
  stop_connection_watchers(peer);

  std::size_t cached_generation = reinterpret_cast<connection_slot*>(&peer)->generation();
  // WARNING!! -> remove id paramater no longer needed.
  connect_update new_connect { .peer=&peer, .socket=peer.tcp.get_socket(), .id=0, .generation=cached_generation };
  (void)connects.queue.push(std::move(new_connect));
  connects.consumer.send();

  statistics.increment_connected_bittorrent_peers();
  if (peer.source == psource::tcp_server)
    statistics.single_outbound_resolved();
  else {
    statistics.single_inbound_resolved();
    inbound_connection_scheduler.send_notification();
  }
}

void PeerConnectionManager::server_socket_callback(ev::io& server, int event){
  (void)event;(void)server;

  bool pending_accepts = true;
  auto has_outbound_inflight_capacity = [&] {
    return  statistics.get_outbound_inflight() < bprotocol::constants::max_outbound_inflight;
  };

  while ( !statistics.has_met_connection_quota() && has_outbound_inflight_capacity() && pending_accepts )
     pending_accepts = accept_peer_connection();

}

int PeerConnectionManager::get_listening_port() {
  return outbound_connection_server.parameters.port;
}

beamable_spsc_t<ipv4_peer_address, 100>& PeerConnectionManager::get_ipv4_consumer() {
  return discoveries.beamable_spsc;
}

void PeerConnectionManager::start_manager() {
  event_loop.run();
}

connection_statistics_t::connection_statistics_t(ev::dynamic_loop& loop, double tick_duration)
  : statistics_printer(loop)
{
  statistics_printer.set<connection_statistics_t, &connection_statistics_t::tick_printer>(this);
  statistics_printer.set(tick_duration, tick_duration);
}

bool connection_statistics_t::has_met_connection_quota() {
  if (connected_bittorrent_peers < bprotocol::constants::healthy_peer_count)
    return false;
  else
    return true;
}


// I DID NOT WRITE THE TICK_PRINTER CODE BELOW.

void connection_statistics_t::tick_printer() {

  static bool first_run = true;
  static const bool is_tty = isatty(fileno(stdout));

  const std::vector<std::pair<const char*, std::size_t>> stats = {
    { "connected peers",   connected_bittorrent_peers },
    { "inbound inflight",  inbound_inflight           },
    { "outbound inflight", outbound_inflight          },
    { "failed peers",      failed_peers               },
    { "peers in retry",    peers_in_retry             },
    { "ipv4 cache",        ipv4_addr_in_cache         },
  };

  constexpr int label_width = 18;
  const int line_count = static_cast<int>(stats.size());

  if (is_tty && !first_run) std::printf("\033[%dA", line_count);

  first_run = false;

  for (auto& [label, value] : stats) {
    if (is_tty) std::printf("\033[K");
      std::printf("%-*s: %zu\n", label_width, label, value);
  }

  std::fflush(stdout);

}
