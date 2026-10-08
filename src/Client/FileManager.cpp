#include "FileManager.hpp"

void FileManager::create_piece_map(TorrentFile & metainfo) {

  const auto piece_length = metainfo.get_piece_length();

  if (metainfo.torrent_is_file()) {

    int file_fd = -1;

    for (std::size_t piece_idx = 0; piece_idx < metainfo.get_piece_count(); ++piece_idx) {
      piece_map[piece_idx].push_back({
        file_fd,
        piece_idx * piece_length,
        static_cast<std::size_t>(piece_length)
      });
    }

  }

  else {

    const auto& files = metainfo.files().value().get();

    std::size_t current_piece = 0;

    std::size_t prev_piece_remainder = piece_length;

    for ( const auto& file : files ) {

      const auto& file_map = file.get_as<ben::decoded_type::dictionary>();

      const std::size_t file_size = static_cast<std::size_t> (
        (*file_map.find("length"))
        .second
        .get_as<ben::decoded_type::integer>()
      );

      const auto& file_path =
        (*file_map.find("path"))
        .second
        .get_as<ben::decoded_type::list>();

      // initialize file here
      int current_fd{-1};
      (void) file_path;
      // end file initialization here

      std::size_t file_byte_offset = 0;
      std::size_t remaining_size = file_size;

      if ( file_size >= prev_piece_remainder && prev_piece_remainder < std::size_t(piece_length) ) {
        piece_map[current_piece].push_back({
          current_fd,
          file_byte_offset,
          prev_piece_remainder
        });
        remaining_size -= prev_piece_remainder;
        prev_piece_remainder = piece_length;
        ++current_piece;
        ++file_byte_offset;
        piece_map[current_piece].shrink_to_fit();
      }

      for (
        auto complete_piece_size = file_size / piece_length
        ; file_byte_offset < complete_piece_size
        ; file_byte_offset++, ++current_piece, remaining_size-=piece_length
      ) {
        piece_map[current_piece].push_back({
          current_fd,
          file_byte_offset * piece_length,
          static_cast<std::size_t>( piece_length )
        });
        piece_map[current_piece].shrink_to_fit();
      }

      if ( remaining_size ) {
        piece_map[current_piece].push_back({
          current_fd,
          file_byte_offset * piece_length,
          remaining_size
        });
        prev_piece_remainder = piece_length - remaining_size;
      }

    }

  }

}
