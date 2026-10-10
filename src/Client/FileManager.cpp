#include "FileManager.hpp"
#include <algorithm>
#include <cstdlib>

piece_mapper_t::piece_mapper_t(TorrentFile& torrent, file_mapper_t& file_mapper)
  : piece_length(torrent.get_piece_length()) , file_ranges(torrent.get_file_count())
{

  if (torrent.torrent_is_file()) {

    file_ranges[0] = { 0, static_cast<std::size_t>( torrent.get_download_size() ) };

    //file_mapper.preallocate("null", file_ranges[0], 0);

    return;

  }

  const auto& files = torrent.files().value().get();

  std::size_t start_byte = 0;

  for (std::size_t file_index = 0; const auto& file : files) {

    const auto& file_map =
      file
      .get_as<ben::decoded_type::dictionary>();

    const std::size_t file_size =
      (*file_map.find("length"))
      .second
      .get_as<ben::decoded_type::integer>();

    file_ranges[file_index] =  {start_byte, file_size + start_byte};

    //file_mapper.preallocate("null", file_ranges[file_index], file_index);

    start_byte += file_size;

    ++file_index;
  }

}

std::vector<file_piece_t> piece_mapper_t::files_for(std::size_t piece_index) {

  static constexpr auto comparator = [](const file_range& a, std::size_t b){ return a.end_byte < b; };

  const std::size_t piece_byte_mark = piece_index * piece_length;

  auto found = std::lower_bound(file_ranges.begin(), file_ranges.end(), piece_byte_mark, comparator);

  std::size_t file_idx = found - file_ranges.begin();

  std::vector<file_piece_t> files;

  std::size_t piece_remaining = piece_length;

  for (; file_idx < file_ranges.size() && piece_remaining != 0; ++file_idx) {

    const auto& file_map  = file_ranges[file_idx];

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
