#include "../Client/TorrentFile.hpp"
#include "../Client/FileManager.hpp"
#include <ios>
#include "../Client/Hasher.hpp"

int main(int argc, char* argv[]) {

  if (argc != 2) { std::cerr << "give a metainfo file\n"; return 1; }

  TorrentFile tfile{argv[1]};
  FileManager file(tfile);
  file.create_piece_map(tfile);

  std::cout << std::boolalpha;
  std::cout << "Torrent Hash:\t\t" << Hasher::hex_stringify_hash(tfile.get_info_hash()) << '\n';
  std::cout << "Torrent Name:\t\t" << tfile.get_torrent_name() << '\n';
  std::cout << "Torrent Size:\t\t" << tfile.get_download_size() << '\n';
  std::cout << "Torrent Piece length:\t" << tfile.get_piece_length() << '\n';
  std::cout << "Torrent Piece Hash[1]:\t" << tfile.get_piece_hash(0) << '\n';
  std::cout << "Torrent File?:\t\t" << tfile.torrent_is_file() << '\n';
  std::cout << "Torrent Piece Count:\t" << tfile.get_piece_count() << '\n';

  std::size_t index = 0;
  for (const auto& piece_entry : file.piece_map) {
    std::cout << index++ << " -> ";
    for (const auto& file_span: piece_entry) {
      std::cout << "{ " << file_span.file << ", " << file_span.offset << ", " <<  file_span.size << " }";
      std::cout << ", ";
    }
    std::cout << '\n';
  }
}
