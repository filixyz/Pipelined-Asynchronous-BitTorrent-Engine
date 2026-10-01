#pragma once
#include "PeerManagerTypes.hpp"
#include "ThreadMessageTypes.hpp"
#include "TorrentFile.hpp"
#include "PeerConnectionManager.hpp"

class PeerManager {

  peer_connects_queue_t bittorrent_connects;
  PeerConnectionManager connection_manager;

public:
  PeerManager(TorrentFile& torrent) : connection_manager(torrent, bittorrent_connects) {}

  inline beamable_spsc_t<peer_contact, 100>& get_peer_contacts_consumer() {
    return connection_manager.get_peer_contacts_consumer();
  }

  inline int get_listening_port() {
    return connection_manager.get_listening_port();
  }

  inline void start_connection_manager_on_current_thread() {
    connection_manager.start_manager();
  }

};
