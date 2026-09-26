#include "../Client/TrackerManager.hpp"
#include "../Client/PeerManager.hpp"
#include "../Client/InitCurl.hpp"
#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {

  if (argc != 2) { std::cerr << "give a metainfo file\n"; return 1; }

  InitCurl initialize_curl{};

  TorrentFile     torrent       {argv[1]};
  PeerManager     peer_manager  {torrent};
  TrackerManager  tracker       {torrent, peer_manager.get_listening_port(), peer_manager.get_ipv4_consumer() };

  std::jthread peer_daemon {
    &PeerManager::start_connection_manager_on_current_thread, &peer_manager
  };

  std::jthread tracker_daemon {
    &TrackerManager::start_tracker_manager, &tracker
  };

  std::this_thread::sleep_for(std::chrono::seconds(10));
  tracker.start();

}
