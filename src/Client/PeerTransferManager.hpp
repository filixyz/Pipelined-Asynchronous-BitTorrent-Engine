#ifndef TRANSFER_MAN
#define TRANSFER_MAN

#include "PeerManagerTypes.hpp"
#include <ev++.h>

class PeerTransferManager {

  // ev::timer rank_timer;
  // ev::io event_loop;
  std::vector<PeerSession> peer_connections{};

};

#endif
