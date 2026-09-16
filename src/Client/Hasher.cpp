#include "Hasher.hpp"
#include <crypto++/sha.h>
#include <cstddef>
#include <span>
#include <sstream>
#include <string>
#include <array>

const std::array<std::byte, 20> Hasher::get_sha1(const std::span<const std::byte> data) {
  CryptoPP::SHA1 hash;
  std::array<std::byte, 20> digest;
  hash.Update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  hash.Final(reinterpret_cast<unsigned char*>(&digest[0]));
  return digest;
}

template <std::size_t N>
std::string Hasher::hex_stringify_hash(const std::span<std::byte, N>& byte_sequence ) {
  std::stringstream hex_stream;
  hex_stream << std::hex;
  for(const std::byte& byt: byte_sequence) {
    unsigned decimal_value = std::to_integer<unsigned>(byt);
    hex_stream << (decimal_value<16 ? (hex_stream<<0,decimal_value) : decimal_value);
  }
  return hex_stream.str();
}

template <std::size_t N>
std::string Hasher::byte_stringify_hash(const std::span<const std::byte, N>& byte_sequence) {
  std::string byte_string;
  for(auto byt : byte_sequence)
    byte_string += static_cast<std::string::value_type>(byt);
  return byte_string;
}
