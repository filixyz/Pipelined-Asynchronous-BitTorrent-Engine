#pragma once
#include <cstdint>
#include <sys/socket.h>
#include <cstddef>
#include <array>

namespace bprotocol::constants {

  consteval std::array<char, 20> get_client_id() {
    std::array<char, 20> id;
    const char id_with_null  [21] = "FJ0001-x4Kn8mR2pL9sq";
    for (auto i = 0; i < 20; ++i)
      id[i] = id_with_null[i];
    return id;
  }

  inline constexpr std::array <char, 20> client_id = get_client_id();
  inline constexpr std::array <std::uint8_t, 8> reserved_bytes {0};
  inline constexpr std::size_t healthy_peer_count = 50;
  inline constexpr std::size_t tcp_bufexp = 16; // 2^tcp_bufexp: if 16 65,636 bytes as buffer size
  inline constexpr std::size_t hanshake_bufexp = 7; // 128 bytes.
  inline constexpr std::size_t max_inbound_inflight = 35;
  inline constexpr std::size_t max_outbound_inflight = 15;
  inline constexpr std::size_t connection_backlog = 128;
  inline constexpr std::size_t rankify_duration = 10; //secs

  namespace peer {
    constexpr std::size_t max_reties = 3;
    constexpr std::size_t connect_timeout = 10;//seconds;
    constexpr std::size_t retry_timeout = 15;//seconds;
  }

  namespace file {

  };

}
