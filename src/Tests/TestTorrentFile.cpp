#include "../Client/TorrentFile.hpp"
#include <ios>
#include "../Client/Hasher.hpp"

int main(int argc, char* argv[]) {

  if (argc != 2) { std::cerr << "give a metainfo file\n"; return 1; }

  TorrentFile tfile{argv[1]};

  std::cout << std::boolalpha;
  std::cout << "Torrent Hash:\t\t" << Hasher::hex_stringify_hash(tfile.get_info_hash()) << '\n';
  std::cout << "Torrent Name:\t\t" << tfile.get_torrent_name() << '\n';
  std::cout << "Torrent Size:\t\t" << tfile.get_download_size() << '\n';
  std::cout << "Torrent Piece length:\t" << tfile.get_piece_length() << '\n';
  std::cout << "Torrent Piece Hash[1]:\t" << tfile.get_piece_hash(0) << '\n';
  std::cout << "Torrent File?:\t\t" << tfile.torrent_is_file() << '\n';
  std::cout << "Torrent Piece Count:\t" << tfile.get_piece_count() << '\n';

  const auto& urls = tfile.get_tracker_urls();
  std::cout << "Trackers\n";
  for (auto url : urls) {
    std::cout << url << '\n';
  }

}
