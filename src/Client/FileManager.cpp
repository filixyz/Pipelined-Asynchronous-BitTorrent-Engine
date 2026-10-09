#include "FileManager.hpp"

void FileManager::create_piece_map(TorrentFile & metainfo) {

  piece_map.resize(metainfo.get_piece_count());

  const auto piece_length = static_cast<std::size_t>( metainfo.get_piece_length() );

  if (metainfo.torrent_is_file()) {

    for (std::size_t piece_idx = 0; piece_idx < metainfo.get_piece_count(); ++piece_idx) {

      piece_map[piece_idx].push_back({std::size_t{0}, piece_idx * piece_length, piece_length});

    }

  }

  else {

    const auto& files = metainfo.files().value().get();

    std::size_t current_piece = 0;

    std::size_t prev_piece_remainder = 0;

    for (std::size_t file_index = 0; const auto& file : files) {

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
      (void) file_path;
      // end file initialization here

      std::size_t file_byte_offset = 0;
      std::size_t remaining_size = file_size;

      if ( prev_piece_remainder && remaining_size >= prev_piece_remainder ) {

        piece_map[current_piece].push_back( { file_index,  file_byte_offset, prev_piece_remainder} );
        piece_map[current_piece].shrink_to_fit();
        ++current_piece;
        remaining_size -= prev_piece_remainder;
        file_byte_offset += prev_piece_remainder;
        prev_piece_remainder = 0;

      }

      while( remaining_size >= static_cast<std::size_t>( piece_length ) ) {

        piece_map[current_piece].push_back( { file_index, file_byte_offset, piece_length} );
        piece_map[current_piece].shrink_to_fit();
        ++current_piece;
        file_byte_offset+=piece_length;
        remaining_size-=piece_length;

      }

      if ( remaining_size ) {

        piece_map[current_piece].push_back( { file_index, file_byte_offset, remaining_size} );
        prev_piece_remainder = (prev_piece_remainder ? prev_piece_remainder : piece_length)- remaining_size;

      }

      file_index += 1;
    }

  }

}
