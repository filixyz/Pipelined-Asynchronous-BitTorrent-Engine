#include "FileManager.hpp"
#include <algorithm>
#include <cstdlib>

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


void piece_map2_t::create_file_spans(TorrentFile& torrent) {

  piece_length = torrent.get_piece_length();

  if (torrent.torrent_is_file()) {

    file_spans.resize(1);

    file_spans[0] = { 0, static_cast<std::size_t>( torrent.get_download_size() ) };

    return;

  }

  const auto& files = torrent.files().value().get();

  file_spans.resize(files.size());

  std::size_t start_byte = 0;

  for (std::size_t file_index = 0; const auto& file : files) {

    const auto& file_map =
      file
      .get_as<ben::decoded_type::dictionary>();

    const std::size_t file_size =
      (*file_map.find("length"))
      .second
      .get_as<ben::decoded_type::integer>();

    file_spans[file_index] =  {start_byte, file_size + start_byte};

    start_byte += file_size;

    ++file_index;
  }

}

std::vector<verified_piece_t> piece_map2_t::get_piece_files(std::size_t piece_index) {

  static constexpr auto comparator = [](const file_range& a, std::size_t b){ return a.end_byte < b; };

  const std::size_t piece_byte_mark = piece_index * piece_length;

  auto found = std::lower_bound(file_spans.begin(), file_spans.end(), piece_byte_mark, comparator);

  std::size_t file_idx = found - file_spans.begin();

  std::vector<verified_piece_t> files;

  std::size_t piece_remaining = piece_length;

  for (; file_idx < file_spans.size() && piece_remaining != 0; ++file_idx) {

    const auto& file_map  = file_spans[file_idx];

    std::size_t file_start = 0;
    std::size_t available_in_file = file_map.end_byte - file_map.start_byte;

    // skip ghost files with file sizes equal to zero
    // and files where the piece byte mark is exactly where the current file
    // ended meaning this piece contributes nothing to it.

    if (!available_in_file or piece_byte_mark == file_map.end_byte) continue;

    if ( piece_byte_mark > file_map.start_byte ) {
      file_start = piece_byte_mark - file_map.start_byte;
      available_in_file = file_map.end_byte - piece_byte_mark;
    }

    std::size_t writable = std::min( available_in_file, piece_remaining );

    files.push_back({ file_idx, file_start, writable });

    piece_remaining -= writable;

  }

  return files;
}
