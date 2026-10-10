#pragma once
#include "TorrentFile.hpp"
#include "ThreadMessageTypes.hpp"
#include <vector>

struct file_piece_t {
  std::size_t file;
  std::size_t offset;
  std::size_t size;
};

struct download_piece_t {
  std::size_t index;
  std::vector<std::byte> piece;
};

struct upload_piece_t {
  std::size_t index;
  std::size_t offset;
  std::vector<std::byte> block; // block.size() should be the logical size of block
};

struct file_range {
  std::size_t start_byte;
  std::size_t end_byte;
};

using incoming_pieces_t =
  beamable_spsc_t<download_piece_t, 50>;

using outgoing_pieces_t =
  beamable_spsc_t<upload_piece_t, 50>;

class piece_mapper_t;

class file_mapper_t {   friend piece_mapper_t;

  std::vector<int> f_descriptors;
  bool preallocate(std::string path, file_range& ref, std::size_t index);

public:

  file_mapper_t(std::size_t size): f_descriptors(size) {}

  // empty files dont need a file descriptor but need to be allocated for
  // file index compliance with piece mapper file indexing hence why ::get_handle_for
  // returns an oprional<int>

  std::optional<int> get_handle_for(std::size_t file_idx);

};

class piece_mapper_t {

  const std::size_t piece_length;
  std::vector<file_range> file_ranges;

public:

  piece_mapper_t( TorrentFile&, file_mapper_t& );
  std::vector<file_piece_t> files_for(std::size_t piece_idx);

};

struct FileManager {

  file_mapper_t file_map;
  piece_mapper_t piece_map;

  //incoming_pieces_t piece_producer;
  //outgoing_pieces_t& piece_consumer;

  FileManager(TorrentFile& torrent):
    file_map(torrent.get_file_count()), piece_map(torrent, file_map)
  {}


  //void store_piece(download_piece_t&);
  //bool retrieve_piece(upload_piece_t&);
  //void store_piece(download_piece_t&);
  //bool retrieve_piece(upload_piece_t&);

  //FileManager(TorrentFile&);

};
