#ifndef TORRENT_FILE
#define TORRENT_FILE

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <array>
#include <functional>
#include <span>
#include "../Bencode/Ben.hpp"

class TorrentFile {

  ben::decoded_type::dictionary transcibe;
  std::array<std::byte, 20> info_hash_byte;
  bool is_file{};
  std::int64_t download_size {0};
  std::size_t piece_count{0};
  std::size_t file_count{0};

  bool parse_transcription();

  const ben::decoded_type::dictionary& info_map() const;
  const ben::decoded_type::string& pieces() const;

public:

  TorrentFile() = delete;
  TorrentFile(const std::filesystem::path pathname);

public:

  std::vector<std::string_view> get_tracker_urls() const;;
  std::string_view get_torrent_name() const;
  std::int64_t get_piece_length() const;
  std::string_view get_piece_hash(std::size_t index) const;
  bool torrent_is_file() const;
  std::int64_t get_download_size() const;
  std::span<const std::byte> get_info_hash() const;
  std::size_t get_piece_count() const;
  std::optional<std::reference_wrapper< const ben::decoded_type::list>> files() const;
  std::size_t get_file_count() const;

};

#endif
