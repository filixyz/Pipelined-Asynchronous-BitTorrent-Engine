#include "../Client/TorrentFile.hpp"
#include "../Client/FileManager.hpp"
#include <ios>
#include <string>
#include "../Client/Hasher.hpp"

int main(int argc, char* argv[]) {

  if (argc != 3) { std::cerr << "give a metainfo file and piece index\n"; return 1; }

  TorrentFile tfile{argv[1]};
  FileManager disk (tfile);

  std::size_t piece_index = std::stoull(argv[2]);

  auto p10 = disk.piece_map.files_for(piece_index);

  std::cout << "Piece: " << piece_index << " : ";
  for (const auto& file_span: p10) {
    std::cout << "{ " << file_span.file << ", " << file_span.offset << ", " <<  file_span.size << " }";
    std::cout << ", ";
  }
  std::cout << '\n';

  std::cout << std::boolalpha;
  std::cout << "Torrent Hash:\t\t" << Hasher::hex_stringify_hash(tfile.get_info_hash()) << '\n';
  std::cout << "Torrent Name:\t\t" << tfile.get_torrent_name() << '\n';
  std::cout << "Torrent Size:\t\t" << tfile.get_download_size() << '\n';
  std::cout << "Torrent Piece length:\t" << tfile.get_piece_length() << '\n';
  std::cout << "Torrent Piece Hash[1]:\t" << tfile.get_piece_hash(0) << '\n';
  std::cout << "Torrent File?:\t\t" << tfile.torrent_is_file() << '\n';
  std::cout << "Torrent Piece Count:\t" << tfile.get_piece_count() << '\n';
  std::cout << "Torrent files:\t\t" << tfile.get_file_count() << '\n';

}
