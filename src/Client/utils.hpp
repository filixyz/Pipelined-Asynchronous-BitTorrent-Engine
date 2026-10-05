#pragma once

#include <cstdint>

namespace utils {

inline constexpr std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) noexcept {
  return a / b + ( a % b != 0);
}

};
