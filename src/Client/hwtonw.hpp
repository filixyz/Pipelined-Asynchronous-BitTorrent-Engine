#pragma once

#include <bit>
#include <cstdint>

constexpr std::uint64_t h_word_to_n_word(std::uint64_t word) {

  if constexpr (std::endian::native == std::endian::little) {
    return (
      (word & 0x00000000000000FFULL) << 56 |
      (word & 0x000000000000FF00ULL) << 40 |
      (word & 0x0000000000FF0000ULL) << 24 |
      (word & 0x00000000FF000000ULL) <<  8 |
      (word & 0x000000FF00000000ULL) >>  8 |
      (word & 0x0000FF0000000000ULL) >> 24 |
      (word & 0x00FF000000000000ULL) >> 40 |
      (word & 0xFF00000000000000ULL) >> 56
    );
  } else
    return word;

}
