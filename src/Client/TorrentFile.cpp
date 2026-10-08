#include "TorrentFile.hpp"
#include "utils.hpp"
#include "../Errorhandlers/BittorentErrors.hpp"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <optional>
#include <span>
#include "Hasher.hpp"

TorrentFile::TorrentFile(const std::filesystem::path pathname) {

  std::ifstream torrent_file{pathname, std::ios::in | std::ios::binary | std::ios::ate};

  if (!torrent_file) throw Torrent_File_Not_Found{};

  std::streamsize size = torrent_file.tellg();
  torrent_file.seekg(0, std::ios::beg);

  std::string source_buffer;
  source_buffer.resize(size);

  auto& read_state = torrent_file.read(source_buffer.data(), size);

  if (!read_state) throw Invalid_Torrent_File{};

  auto decode = ben::decode::input(source_buffer);

  if (!decode) throw Invalid_Torrent_File{};

  if (decode.value().type() != ben::encode_type::dictionary) throw Invalid_Torrent_File{};

  transcibe = std::move(decode.value().get_as<ben::decoded_type::dictionary>());

  if (auto found = ben::find("info", transcibe)) { // get info hash

    auto& info_value = *found;
    auto encode = info_value.get_position_in_source();
    auto byte_view = std::as_bytes(std::span( &source_buffer[encode.start], encode.size ));
    info_hash_byte = Hasher::get_sha1( byte_view );

  } else throw Invalid_Torrent_File{};

  if (!parse_transcription()) throw Invalid_Torrent_File {};

}

bool TorrentFile::parse_transcription() {

  if (

    !info_map().contains("name")           and
    !info_map().contains("pieces")         and
    !info_map().contains("piece length")

  ) return false;

  if (auto found = ben::find( "length", info_map() )) {

    is_file = true;
    auto& length = *found;
    download_size = length.get_as<ben::decoded_type::integer>();

  }

  else if ( auto found = ben::find( "files", info_map() ) ) {

    is_file = false;
    auto files = (*found).get_as<ben::decoded_type::list>();
    for ( auto& file : files ) {
      auto file_map = file.get_as<ben::decoded_type::dictionary>();

      if ( auto found = ben::find("length", file_map) )
        download_size += (*found).get_as<ben::decoded_type::integer>();
      else return false;

      if ( !file_map.contains("path") ) return false;
    }

  }

  else return false;

  if (pieces().length() % 20 != 0) return false;

  piece_count = pieces().length()/20;

  // validate that all pieces correllate mathematically

  if (utils::ceil_div(download_size, get_piece_length()) != piece_count)  return false;

  return true;

}

std::vector<std::string_view> TorrentFile::get_tracker_urls() const {

  std::vector<std::string_view> trackers;

  if ( auto found = ben::find( "announce", transcibe) ) {
    auto& announce_url = (*found).get_as<ben::decoded_type::string>();
    trackers.push_back(announce_url);
  }

  if ( auto found = ben::find( "announce-list", transcibe) ) {

    auto& url_pack = (*found).get_as<ben::decoded_type::list>();

    for (const auto& url_list : url_pack)
    for (const auto& url : url_list.get_as<ben::decoded_type::list>())
      trackers.push_back(url.get_as<ben::decoded_type::string>());

  }

  return trackers;

}

const ben::decoded_type::string& TorrentFile::pieces() const {

  return
    (*info_map().find("pieces"))
    .second
    .get_as<ben::decoded_type::string>();

}

std::string_view TorrentFile::get_torrent_name() const {

  return
    (*info_map().find("name"))
    .second
    .get_as<ben::decoded_type::string>();

}

std::int64_t TorrentFile::get_piece_length() const {

  return
    (*info_map().find("piece length"))
    .second
    .get_as<ben::decoded_type::integer>();

}

const ben::decoded_type::dictionary& TorrentFile::info_map() const {

  return
    (*transcibe.find("info"))
    .second
    .get_as<ben::decoded_type::dictionary>();

}

std::string_view TorrentFile::get_piece_hash(std::size_t index) const {

  assert( index < piece_count );

  constexpr static std::size_t hash_length = 20;

  const auto& piece_hashes = pieces();

  int hash_index = index * hash_length;

  return std::string_view(&piece_hashes[hash_index], hash_length);

}

bool TorrentFile::torrent_is_file() const {
  return is_file;
}

std::int64_t TorrentFile::get_download_size() const {
  return download_size;
}

std::span<const std::byte> TorrentFile::get_info_hash() const {
  return info_hash_byte;
}

std::size_t TorrentFile::get_piece_count() const {
  return piece_count;
}


std::optional<std::reference_wrapper< const ben::decoded_type::list>> TorrentFile::files() const {

  if (is_file) return std::nullopt;

  return
    (*info_map().find("files"))
    .second
    .get_as<ben::decoded_type::list>();

}
