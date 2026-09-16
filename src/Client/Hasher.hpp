#ifndef HASHER
#define HASHER

#include <cstdint>
#include <string>
#include <span>

struct Hasher{
  static const std::array<std::byte, 20> get_sha1(const std::span<const std::byte>);
  static bool test_buffer_to_sha1(const std::span<const std::byte>, std::string);
  template <std::size_t N>  static std::string hex_stringify_hash(const std::span<std::byte, N>&);
  template <std::size_t N>  static std::string byte_stringify_hash(const std::span<const std::byte, N>&);

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
