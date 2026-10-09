#pragma once
#include "TorrentFile.hpp"
#include "ThreadMessageTypes.hpp"

struct download_piece_t {
  std::size_t index;
  std::vector<std::byte> piece;
};

struct upload_piece_t {
  std::size_t index;
  std::size_t offset;
  std::vector<std::byte> block; // block.size() should be the logical size of block
};

struct verified_piece_t {
  int file;
  std::size_t offset;
  std::size_t size;
};

using piece_span_t = typename std::vector<verified_piece_t>;
using piece_map_t = typename std::vector<piece_span_t>;
using incoming_pieces_t = beamable_spsc_t<download_piece_t, 50>;
using outgoing_pieces_t = beamable_spsc_t<upload_piece_t, 50>;

struct FileManager {

  piece_map_t piece_map;
  //incoming_pieces_t piece_producer;
  //outgoing_pieces_t& piece_consumer;

  void create_piece_map(TorrentFile&);

  //void store_piece(download_piece_t&);
  //bool retrieve_piece(upload_piece_t&);

  //FileManager(TorrentFile&);

};
