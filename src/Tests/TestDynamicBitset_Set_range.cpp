#include "../Client/DynamicBitset.hpp"
#include <array>
#include <cstdint>
#include <ios>
#include <iostream>

int main() {

  DynamicBitset set (10);
  std::array<std::uint8_t, 2> payload {0X2F, 0XC0};

  auto success = set.decode_as_payload( payload );
  std::cout << std::boolalpha << success << '\n';

  std::cout << "all: " << set.all() << '\n';
  std::cout << "any: "<< set.any() << '\n';
  std::cout << "none: " << set.none() << '\n';
  std::cout << "count: " << set.count() << '\n';

  std::cout << "First set index = " << set.find_first() << '\n';
  std::cout << "First set index = " << set.find_next(set.find_first()) << '\n';

  for (auto index : set.set_bits()) {
    std::cout << "bit " << index << " is set\n";
  }

  set.clear();
  std::cout << "any: "<< set.any() << '\n';

}
