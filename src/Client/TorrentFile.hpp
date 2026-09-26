#ifndef TORRENT_FILE
#define TORRENT_FILE

#include "../Bencoder/Bencode.hpp"
#include <cstdint>
#include <filesystem>
#include <array>
#include <span>

class TorrentFile {
private:
  std::map<std::string, Bendata> transcibe;
  const std::map<std::string, Bendata> *info_hash;
  std::array<std::byte, 20> info_hash_byte;
  std::int64_t file_size {0};
  void check_validity_of_transcribe() const;
  void initialize_info_hash_bytes();
  void compute_download_size();

public:
  TorrentFile(const std::filesystem::path pathname);
  TorrentFile() = delete;
  std::vector<std::string_view> get_tracker_urls() const;
  std::string_view get_info_key() const;
  std::string_view get_torrent_name() const;
  std::span<const std::byte> get_info_hash_bytes() const;
  std::int64_t get_piece_length() const;
  std::string_view get_piece_hash(int index) const;
  bool torrent_is_file() const;
  std::int64_t get_download_size() const;
};

#endif
