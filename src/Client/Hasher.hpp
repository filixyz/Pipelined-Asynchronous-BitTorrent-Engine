#ifndef HASHER
#define HASHER

#include <cstdint>
#include <string>
#include <span>
#include "sha1.hpp"
#include <array>
#include <sstream>

struct Hasher {

  inline static const std::array<std::byte, 20> get_sha1 (const std::span<const std::byte> data) {
    sha1::SHA1 hash;
      std::array<std::byte, 20> digest;
      hash.processBytes(data.data(), data.size());
      hash.getDigestBytes(reinterpret_cast<std::uint8_t*>(digest.data()));
    return digest;
  }

  template <std::size_t N>  static std::string hex_stringify_hash(const std::span<std::byte, N>& byte_sequence) {
    std::stringstream hex_stream;
    hex_stream << std::hex;
    for (const std::byte& byt: byte_sequence) {
      unsigned decimal_value = std::to_integer<unsigned>(byt);
      hex_stream << (decimal_value<16 ? (hex_stream<<0,decimal_value) : decimal_value);
    }
    return hex_stream.str();

  }

  template <std::size_t N>  static std::string byte_stringify_hash(const std::span<const std::byte, N>& byte_sequence) {
    std::string byte_string;
    for(auto byt : byte_sequence)
      byte_string += static_cast<std::string::value_type>(byt);
    return byte_string;
  }

  inline static std::uint64_t fnv_1a_64bits (std::span<const std::byte> sequence) {

    static constexpr std::uint64_t FNV_prime_64 = 1099511628211;
    static constexpr std::uint64_t offset_basis_64 = 14695981039346656037ULL;

    std::uint64_t hashed_value = offset_basis_64;
    for (auto& byte : sequence)
      hashed_value = (hashed_value ^ static_cast<std::uint8_t>(byte)) * FNV_prime_64;

    return hashed_value;
  }

};

#endif
