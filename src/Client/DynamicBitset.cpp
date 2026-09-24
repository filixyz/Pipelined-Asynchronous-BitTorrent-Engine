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

constexpr DynamicBitset::bitfield_index DynamicBitset::get_index(std::size_t index) const {

  return { index / 64, 63 - (index & 63) };

}

DynamicBitset::DynamicBitset(std::size_t count) : bitfield(words_length(count)), length(count) {};

bool DynamicBitset::decode_wire_bytes (std::span<const std::uint8_t> bytes_view) {

  // This decoder assumes that caller has already cleared the
  // bitset if not clear whatever bits set remains since this
  // population compute is basically an ORing

  if (bytes_view.size() != bytes_length(length))  return false;

  std::size_t remains = length & 7;
  std::uint8_t end_byte = bytes_view.back();

  if ( (remains != 0) and (end_byte & (UINT8_MAX>>remains)) )
    return false;

  std::size_t  word_offset = 0;
  std::size_t  byte_offset = 7;

  for ( auto& byte : bytes_view ) {

    bitfield[word_offset] |= std::uint64_t(byte)<<(8*byte_offset);

    if ( byte_offset == 0 ) {
      ++word_offset; byte_offset=7; continue;
    }

    --byte_offset;

  }

  return true;

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

void DynamicBitset::print() const {
  for (auto& i : bitfield)
    std::cout << std::bitset<64>(i).to_string() << '\n';
}


bool DynamicBitset::test(std::size_t index) const {

  if (index >= length) return false;

  auto bit = get_index(index);

  return bitfield[bit.word_index] & std::uint64_t(1)<<bit.index;

}

void DynamicBitset::set(std::size_t index){

  if (index >= length) return;

  auto bit = get_index(index);

  bitfield[bit.word_index] |= std::uint64_t(1)<<bit.index;

}

void DynamicBitset::reset(std::size_t index) {

  if (index >= length) return;

  auto bit = get_index(index);

  bitfield[bit.word_index] &= ~(std::uint64_t(1)<<bit.index);

}

void DynamicBitset::clear() {

  std::fill(bitfield.begin(), bitfield.end(), std::uint64_t{0});

}

std::size_t DynamicBitset::size() const {
  return length;
}

DynamicBitset::set_range_t DynamicBitset::set_bits() const {

  return set_range_t{ *this };

}

std::size_t DynamicBitset::find_first() const {

  auto set_range = set_bits();
  auto first = set_range.begin();
  return first == set_range.end() ? npos : *first;

}

std::size_t DynamicBitset::find_next(std::size_t from) const {

  if (from >= length)  return npos;

  set_range_t::iterator next {*this};
  next.bit = { from / 64 , from & 63 };
  ++next;

  return next == set_bits().end() ? npos : *next;

}

// ------------------------ SET RANGE IMPLEMENTATION----------------- //


DynamicBitset::set_range_t::iterator&  DynamicBitset::set_range_t::iterator::operator++() {

  // operator ++ is a blind set range iterator / incrementer
  // it assumes the current bit spot it reads from has been consumed by the caller
  // and the caller needs a new index that represents a true bit (a set bit)
  // it has no concept of logical end with respect to the actual set its iterating
  // so if left unchecked it will gladly pass the number of logical bits your set
  // should be representing if there are padded ending bits which, god forbid,
  // due to bad implementation, somehow is set, or true or flagged.

  std::size_t current = bit.index + 1;

  for (; bit.word_index < set.bitfield.size(); ++ bit.word_index, current=0 ) {

    if ( current == 64 ) continue;

    std::uint64_t unread_mask = UINT64_MAX >> current;
    std::uint64_t unread = set.bitfield[bit.word_index] & unread_mask;

    if (unread == 0) continue;

    std::size_t set_bit_index = static_cast<std::size_t> ( std::countl_zero(unread) );

    bit.index = set_bit_index;

    return * this;

  }

  bit.index = 0;
  return *this;

  //----------another implementation-----------
  std::size_t next = word_index + 1;
  for (; next < set.bitfield.size(); ++next) {

    if (cached_reads != 0) return *this;

    cached_reads = set.bitfield[next];
    word_index = next;

  }

  return *this;
}

std::size_t DynamicBitset::set_range_t::iterator::operator* () const {

  return (64 * bit.word_index) + bit.index;

  // -------------another implementation-----------
  std::size_t set_bit_index = std::countl_zero(cached_reads);

  if (set_bit_index < 64)
    cached_reads &= ~( std::uint64_t{1}<<( 63 - set_bit_index ) );

  return (64 * bit.word_index) + set_bit_index;

  // meaning end sentinel wil be set.bitfield.size()*64 since
  // in operator++ caching stops when word_index is 1 unit smaller than
  // set.bitfield size and returns the iterator, not the *operator
  // if there no bits left will count the number of consective zeros to
  // be 64 bits, cached_reads is definitely zero so the read index
  // clearing conditonal block is not invoked and return evaluates to
  // 64 * (word_index 1 unit smaller than set.bitfield.size()) + 64
  // which should == set.bitfield.size() * 64

}

bool DynamicBitset::set_range_t::iterator::operator==(const iterator& other) const {
  return (
    &set            ==  &other.set            &&
    bit.word_index  ==  other.bit.word_index  &&
    bit.index       ==  other.bit.index
  );
}

DynamicBitset::set_range_t::iterator DynamicBitset::set_range_t::end() const {

  bitfield_index end_bit = {set.bitfield.size(), 0};
  iterator end_iterator {set};
  end_iterator.bit = end_bit;
  return end_iterator;

}

DynamicBitset::set_range_t::iterator DynamicBitset::set_range_t::begin() const {

  if (set.length == 0)  return end();

  bitfield_index start_bit {0, 0};
  iterator start_iterator {set};
  start_iterator.bit = start_bit;
  return set.test(0) ? start_iterator : ++start_iterator;

}
