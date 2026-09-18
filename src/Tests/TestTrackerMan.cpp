#include "../Client/TrackerManager.hpp"
#include <chrono>
#include <ios>
#include <thread>
int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "give a metainfo file\n";
    return 1;
  }
  std::cout << std::boolalpha;
  TorrentFile torrent {argv[1]};
  TrackerManager tracker{torrent, 1984};
  std::jthread trkr_service {&TrackerManager::test, &tracker, 200};
  std::this_thread::sleep_for(std::chrono::seconds(10));
  tracker.start();
}
