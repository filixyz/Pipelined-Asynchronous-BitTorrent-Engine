#include "TorrentFile.hpp"
#include "../Errorhandlers/BittorentErrors.hpp"
#include <cstdint>
#include <fstream>
#include <span>
#include "Hasher.hpp"

constexpr int HASH_STRING_LENGTH = 20;

TorrentFile::TorrentFile(const std::filesystem::path pathname) {
  std::ifstream torrent_file{pathname};
  if (!torrent_file)
    throw Torrent_File_Not_Found{};
  try {
    Bendata parsed_bendata(bendecode_from_file(torrent_file));
    transcibe = std::move(parsed_bendata.get_data<ben::dic>());
    check_validity_of_transcribe();
    info_hash = &(transcibe.find("info")->second.get_data<ben::dic>());
  } catch (...) {
    throw Invalid_Torrent_File{};
  }
  compute_download_size();
  initialize_info_hash_bytes();
}

void TorrentFile::initialize_info_hash_bytes() {
  std::string_view info_key = get_info_key();
  const std::span<const std::byte> hash_byte_view(reinterpret_cast<const std::byte*>(info_key.data()), info_key.size());
  info_hash_byte = Hasher::get_sha1(hash_byte_view);
}

void TorrentFile::compute_download_size() {
  if ( torrent_is_file() )
    file_size = info_hash->find("length")->second.get_data<ben::num>();
  else {
    auto& files = info_hash->find("files")->second.get_data<ben::lis>();
    for (auto& file : files) {
      file_size += file.get_data<ben::dic>().find("length")->second.get_data<ben::num>();
    }
  }
}

void TorrentFile::check_validity_of_transcribe() const {
  // implement later
}

std::vector<std::string_view> TorrentFile::get_tracker_urls() const {
  std::vector<std::string_view> trackers;
  trackers.push_back( transcibe.find("announce")->second.get_data<std::string>() );
  if ( transcibe.contains("announce-list") ) {
    const std::vector<Bendata>& announce_list = transcibe.find("announce-list")->second.get_data<ben::lis>();
    for (const Bendata& bencoded_url : announce_list)
      for (const Bendata& list : bencoded_url.get_data<ben::lis>())
        trackers.push_back(list.get_data<ben::str>());
  }
  return trackers;
}

std::string_view TorrentFile::get_info_key() const {
  return transcibe.find("info")->second.get_encode();
}
std::string_view TorrentFile::get_torrent_name() const {
  return info_hash->find("name")->second.get_data<ben::str>();
}
std::int64_t TorrentFile::get_piece_length() const {
  return info_hash->find("piece length")->second.get_data<ben::num>();
}

std::string_view TorrentFile::get_piece_hash(int index) const {
  const std::string &pieces_hash =
      info_hash->find("pieces")->second.get_data<ben::str>();
  int hash_index = index * HASH_STRING_LENGTH;
  return std::string_view(&pieces_hash[hash_index], HASH_STRING_LENGTH);
}

bool TorrentFile::torrent_is_file() const {
  auto end_itr = info_hash->end();
  auto file_itr = info_hash->find("length");
  return file_itr != end_itr ? true : false;
}

std::int64_t TorrentFile::get_download_size() const {
  return file_size;
}

std::span<const std::byte> TorrentFile::get_info_hash_bytes() const {
  return info_hash_byte;
}
