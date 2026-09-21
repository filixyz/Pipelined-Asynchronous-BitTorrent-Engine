#include "DynamicBitset.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <bitset>
#include <cassert>
#include <bit>

constexpr std::size_t DynamicBitset::words_length(std::size_t bit_length) const {
  std::size_t extra = bit_length & (63) ? 1 : 0;
  std::size_t nextr = bit_length/64;
  return nextr + extra;
}

constexpr std::size_t DynamicBitset::bytes_length(std::size_t bit_length) const {

  return bit_length / 8 + ( (bit_length & 7)!=0  );

}

constexpr DynamicBitset::bitfield_index DynamicBitset::get_index(std::size_t index) const {

  return { index / 64, 63 - (index & 63) };

}

DynamicBitset::DynamicBitset(std::size_t count) : bitfield(words_length(count)), length(count){
  bitfield.shrink_to_fit();
};

DynamicBitset::DynamicBitset(std::span<const std::uint8_t> bytes_view, std::size_t _length)
  : bitfield(words_length(_length)), length(_length)
{
  using byte_view_t = std::span<const std::uint8_t>;

  assert(bytes_view.size() == bytes_length(length));
  assert(length != 0);

  bitfield.shrink_to_fit();

  std::size_t  word_offset            = 0;
  std::size_t  byte_offset            = 7;
  byte_view_t  bytes_view_minus_back  = bytes_view.first(bytes_view.size()-1);

  for (auto& byte : bytes_view_minus_back ) {

    bitfield[word_offset] |= std::uint64_t(byte)<<(8*byte_offset);

    if   ( byte_offset == 0 )  { ++word_offset; byte_offset=7; }
    else                       { --byte_offset; }

  }

  std::size_t  remainder = 7 & length;
  std::uint8_t preerve_mask = remainder == 0 ?
      0xFF
    : 0xFF << (8 - remainder)
  ;
  std::uint8_t last_byte = bytes_view.back() & preerve_mask;
  bitfield[word_offset] |= std::uint64_t(last_byte) << (8*byte_offset);

}

void DynamicBitset::operator|=(const DynamicBitset& other) {
  assert(length == other.length);
  for (std::size_t i=0; i < bitfield.size(); ++i)
    bitfield[i] |= other.bitfield[i];
}

void DynamicBitset::operator&=(const DynamicBitset& other) {
  assert(length == other.length);
  for (std::size_t i=0; i < bitfield.size(); ++i)
    bitfield[i] &= other.bitfield[i];
}

DynamicBitset DynamicBitset::operator|(const DynamicBitset& other) {
  DynamicBitset newset = *this;
  newset |= other;
  return newset;
}

DynamicBitset DynamicBitset::operator&(const DynamicBitset& other) {
  DynamicBitset newset = *this;
  newset &= other;
  return newset;
}

std::size_t DynamicBitset::count() const {
  std::size_t set_bits = 0;

  for (auto& word : bitfield)
    set_bits += static_cast<std::size_t>( std::popcount(word) );

  return set_bits;
}

bool DynamicBitset::any() const {

  for (auto& word : bitfield)
    if (word != 0) return true;
  return false;

}

bool DynamicBitset::all() const {

  if (length == 0)
    return true;

  std::span without_last_word = std::span(bitfield).subspan(0, bitfield.size()-1);

  for (auto& word : without_last_word) {

    if (word != UINT64_MAX)  return false;

  }

  if (length & 63)
    return static_cast<std::size_t>( std::countl_one(bitfield.back()) ) == (length & 63);

  return bitfield.back() == UINT64_MAX;

}

bool DynamicBitset::none() const {
  return !any();
}

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

std::size_t DynamicBitset::encode_wire_bytes(std::span<std::byte> target, std::size_t encoded_bytes) const {

  std::size_t total = bytes_length(length);
  std::size_t start_word = encoded_bytes / 8;
  std::size_t byte_offset = encoded_bytes % 8;

  assert( encoded_bytes <= total );

  if (target.empty() or encoded_bytes >= total)
    return encoded_bytes;

  for ( ; start_word < bitfield.size(); ++start_word ) {

    std::uint64_t current_word = h_word_to_n_word( bitfield[start_word] );

    // std::byte* start_byte_addr = reinterpret_cast<std::byte*>(&current_word) + byte_offset;
    // std::size_t current_encode = 0;
    // std::size_t target_available = std::min( std::size_t(8), target.size() );
    // for (; current_encode + byte_offset < 8; ++current_encode ) {
    //   if ( encoded_bytes == bytes_length(length) or current_encode == target.size())
    //     break;
    //   target[current_encode] = * ( start_byte_addr + current_encode );
    //   ++encoded_bytes;
    // }
    // target = target.subspan(current_encode)
    // if (byte_offset + current_encode == 8) byte_offset = 0;
    // if (target.empty()) break;

    std::byte*  start_byte                      = reinterpret_cast<std::byte*>(&current_word) + byte_offset;
    std::size_t remaining_encode_bytes          = total - encoded_bytes;
    std::size_t current_word_remaining_bytes    = 8 - byte_offset;

    std::size_t n = std::min( { target.size(), remaining_encode_bytes, current_word_remaining_bytes } );
    std::memcpy(target.data(), start_byte, n);

    encoded_bytes += n;
    target = target.subspan(n);

    if (byte_offset + n == 8) byte_offset = 0;
    if (target.empty()) break;
  }
  return encoded_bytes;
}

void DynamicBitset::print() {
  for (auto& i : bitfield)
    std::cout << std::bitset<64>(i).to_string() << '\n';
}


bool DynamicBitset::test(std::size_t index) const {

  if (index >= length) return false;

  auto bit = get_index(index);

  return bitfield[bit.field_index] & std::uint64_t(1)<<bit.index;

}

void DynamicBitset::set(std::size_t index){

  if (index >= length) return;

  auto bit = get_index(index);

  bitfield[bit.field_index] |= std::uint64_t(1)<<bit.index;

}

void DynamicBitset::reset(std::size_t index) {

  if (index >= length) return;

  auto bit = get_index(index);

  bitfield[bit.field_index] &= ~(std::uint64_t(1)<<bit.index);

}

void DynamicBitset::reset() {

  std::fill(bitfield.begin(), bitfield.end(), std::uint64_t{0});

}

std::size_t DynamicBitset::size() const {
  return length;
}
