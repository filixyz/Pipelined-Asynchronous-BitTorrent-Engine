#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <span>

class DynamicBitset {

  std::vector<std::uint64_t> bitfield;
  std::size_t length{};

  struct bitfield_index {
    std::size_t word_index;
    std::size_t index;
  };

  constexpr bitfield_index get_index(std::size_t) const;

private:

  struct set_range_t
  {
    const DynamicBitset& set;

    struct iterator {
      std::size_t operator*() const;
      iterator& operator++();
      bool operator==(iterator const&) const;
      iterator (const DynamicBitset& set_) : set(set_) {};
    private:
      const DynamicBitset& set;
      std::size_t word_index{0};
      mutable std::uint64_t cached_reads{0};
      friend DynamicBitset;
    };

    set_range_t(const DynamicBitset& set_): set(set_) {};

    iterator end() const;
    iterator begin() const;
  };

public:

  // stole this npos idea from boosts implementation.
  inline constexpr static std::size_t npos = static_cast<size_t>(-1);

  DynamicBitset()=default;
  DynamicBitset(std::size_t count);

  DynamicBitset operator&(const DynamicBitset&);
  void operator&=(const DynamicBitset&);
  DynamicBitset operator|(const DynamicBitset&);
  void operator|=(const DynamicBitset&);

  void set(std::size_t);
  void reset(std::size_t);
  void clear();

  bool test(std::size_t) const;
  std::size_t count() const;
  bool any() const;
  bool all() const;
  bool none() const;

  std::size_t find_first() const ;
  std::size_t find_next(std::size_t from) const;
  set_range_t set_bits() const;

  bool        decode_as_payload (std::span<const std::uint8_t> bitview);
  std::size_t encode_as_payload (std::span<std::byte>, std::size_t encoded) const;

  std::size_t size() const;
  void print() const;

};

inline static constexpr std::size_t words_length(std::size_t bit_length) {

  std::size_t extra = bit_length & (63) ? 1 : 0;
  std::size_t nextr = bit_length/64;
  return nextr + extra;

};

inline static constexpr std::size_t bytes_length(std::size_t bit_length) {

  return bit_length / 8 + ( (bit_length & 7)!=0  );

};
