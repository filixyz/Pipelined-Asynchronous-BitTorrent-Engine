#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <span>

class DynamicBitset {

  struct bitfield_index {
    std::size_t field_index;
    std::size_t index;
  };

  std::vector<std::uint64_t> bitfield;
  const std::size_t length{};

  constexpr std::size_t words_length(std::size_t) const;
  constexpr std::size_t bytes_length(std::size_t) const;
  constexpr bitfield_index get_index(std::size_t) const;

  public:

  DynamicBitset()=default;
  DynamicBitset(std::size_t count);
  DynamicBitset(std::span<const std::uint8_t> bitview, std::size_t count);

  DynamicBitset operator&(const DynamicBitset&);
  void operator&=(const DynamicBitset&);
  DynamicBitset operator|(const DynamicBitset&);
  void operator|=(const DynamicBitset&);

  void set(std::size_t);
  void reset(std::size_t);
  void reset();
  bool test(std::size_t) const;
  std::size_t count() const;
  bool any() const;
  bool all() const;
  bool none() const;
  std::size_t encode_wire_bytes(std::span<std::byte>, std::size_t) const;
  std::size_t size() const;

  void print();

};
