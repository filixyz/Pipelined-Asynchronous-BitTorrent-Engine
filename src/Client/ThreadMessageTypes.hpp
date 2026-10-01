#pragma once
#include "spsc_queue.hpp"
#include <cstdint>
#include <ev++.h>
#include <array>
#include <optional>
// For interacting with peer manager

template<class T, std::size_t N>
struct beamable_spsc_t {
  spsc_queue<T, N> queue;
  ev::async consumer;
};

// n_addr   : Network Ordered Address
// n_port   : Network Ordered Port
// is_v6    : is ipv6 ?
// peer_id  : optional.
struct peer_contact {

  std::array<std::byte, 16> n_addr{};
  std::uint16_t n_port{};
  bool is_v6{};
  std::optional<std::array<std::byte, 20>> peer_id;

  inline bool operator == (const peer_contact& other) const {
    return ( is_v6 == other.is_v6 && n_addr  == other.n_addr && n_port == other.n_port );
  }

};

// For interacting with file_manager
struct verified_piece {};
