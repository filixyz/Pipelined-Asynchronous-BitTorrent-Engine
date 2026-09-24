#include "../Client/DynamicBitset.hpp"
#include <array>
#include <cstdint>
#include <ios>
#include <iostream>

int main() {

  DynamicBitset set (10);
  std::array<std::uint8_t, 2> payload {0XAF, 0XC0};

  auto success = set.decode_as_payload( payload );
  std::cout << std::boolalpha << success << '\n';

  for (auto index : set.set_bits()) {
    std::cout << "bit " << index << " is set\n";
  }

}
