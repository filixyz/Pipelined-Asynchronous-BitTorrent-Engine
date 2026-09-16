#ifndef BITTORRENT_MESSAGES
#define BITTORRENT_MESSAGES

#include "io_ring_buffer.hpp"
#include <algorithm>
#include <array>
#include <span>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/uio.h>

namespace bittorrent_messages {

  namespace length {
    constexpr std::size_t handshake = 68;
  }

  enum handshake_offset: std::uint8_t {
    pstrlen   = 1,
    pstr      = 20,
    reserved  = 28,
    info_hash = 48,
    peer_id   = 68
  };

  namespace header {
    constexpr std::array<std::byte, 0> keepalive;
    constexpr std::array<std::byte, 0> choke;
    constexpr std::array<std::byte, 0> unchoke;
    constexpr std::array<std::byte, 0> interested;
    constexpr std::array<std::byte, 0> not_interested;
    constexpr std::array<std::byte, 0> have;
    constexpr std::array<std::byte, 0> request;
    constexpr std::array<std::byte, 0> cancel;
  };

  using handshake_t = std::array<std::byte, length::handshake>;
  inline constexpr std::uint8_t protocol_string_length = 19;
  inline constexpr std::array<char, 19> protocol_string {'B','i','t','T','o','r','r','e','n','t',' ','p','r','o','t','o','c','o','l'};

  struct encode_result {
    bool complete;
  };

  struct decode_result {
    bool complete;
    bool valid;
  };

  // frame_cursor helps to make encoding for upload easier not useful for recving since recieved message
  // type of message cannot be easily inferred from the onset of transaction without storing a lot of states
  // unike uploads where the caller should now what he should be sening
  // .cursor tells where in the current message did the previous encode stop for later resumption
  // .reset() enables the cursor to be reusable for new messages to be encoded as frames.
  struct frame_cursor {
    std::size_t cursor {0};
    void reset() { cursor = 0; }
  };

  inline auto saturating_sub = [](std::size_t a, std::size_t b) { return a >= b ? a - b : 0; };

  inline static std::span<std::byte> wraparound_steal (prepare_t& prepare, std::size_t size) {
    auto& io_vec = prepare.iovec_array[1];
    std::span<std::byte> stolen = { reinterpret_cast<std::byte*>(io_vec.iov_base) , size };
    std::byte* advanced_addr = stolen.data() + size;
    io_vec.iov_base = advanced_addr;
    io_vec.iov_len -= size;
    return stolen;
  }

namespace handshake {

  // encoder and decoder now does not care if buffer full or empty
  // only operates with returned iovecs.
  // this functions also trust that offset is valid in the context of
  // handshakes size, so between 0 to 68

  template <std::size_t size> encode_result encode( const handshake_t& handshake, io_ring_buffer<size>& buffer, frame_cursor& frame ) {

    encode_result encode_resolve {.complete = false };
    std::size_t encode_cursor = frame.cursor;

    if (encode_cursor >= length::handshake) {
      encode_resolve.complete = true;
      return encode_resolve;
    }

    prepare_t prepare = buffer.prepare_write();
    if (prepare.empty())
      return encode_resolve;

    for (std::size_t i = 0; i < prepare.prepared_iovecs; ++i) {
      auto& io_path = prepare.iovec_array[i];
      if (io_path.iov_len == 0)
        break;

      std::size_t io_length = std::min(length::handshake - encode_cursor, io_path.iov_len);
      std::memcpy(io_path.iov_base ,&handshake[encode_cursor], io_length);
      encode_cursor += io_length;
      buffer.commit_write(io_length);

      if (encode_cursor == length::handshake) {
        encode_resolve.complete = true;
        break;
      }
    }

    frame.cursor = encode_cursor;
    return encode_resolve;
  }

  inline decode_result decode (

    io_ring_buffer<length::handshake>&    buffer,
    std::span<const std::byte>            info_hash_seq,
    std::array<std::byte, 20>&            peer_id_ref

  ) {

    decode_result decode_resolve { .complete=false, .valid=false };

    prepare_t prepare = buffer.prepare_read();

    if (prepare.prepared_bytes() < length::handshake)
      return decode_resolve;

    assert(prepare.prepared_iovecs == 2 or prepare.prepared_iovecs == 1);

    decode_resolve.complete = true;

    for ( std::size_t index = 0, range = pstrlen ; index < prepare.prepared_iovecs; ++index ) {

      std::span<const std::byte> io_view (
        reinterpret_cast<const std::byte*> (prepare.iovec_array[index].iov_base),
        prepare.iovec_array[index].iov_len
      );

      if ( range == pstrlen ) {

        if ( io_view.size() < 1 ) continue;

        decode_resolve.valid = static_cast<std::uint8_t>(io_view[0]) == 0x13;
        io_view = io_view.subspan(1);

        if (!decode_resolve.valid ) break;

        range = pstr;

      }

      if ( range == pstr ) {

        if ( io_view.size() < 19 ) {
          auto remaining = wraparound_steal(prepare, 19 - io_view.size());
          decode_resolve.valid = true;
          (void) remaining;
          range = reserved;
          continue;
        } else {
          decode_resolve.valid = true;
          io_view = io_view.subspan(19);
          range = reserved;
        }

      }

      if ( range == reserved ) {

        if ( io_view.size() < 8 ) {
          auto remaining = wraparound_steal(prepare, 8 - io_view.size());
          decode_resolve.valid = true;
          (void) remaining;
          range = info_hash;
          continue;
        } else {
          decode_resolve.valid = true;
          io_view = io_view.subspan(8);
          range = info_hash;
        }

      }

      if ( range == info_hash ) {

        if ( io_view.size() < 20 ) {
          auto remaining = wraparound_steal(prepare, 20 - io_view.size());
          decode_resolve.valid =
            std::ranges::equal(info_hash_seq.first(io_view.size()), io_view) &&
            std::ranges::equal(info_hash_seq.subspan(io_view.size()), remaining);
          if (!decode_resolve.valid) break;
          range = peer_id;
          continue;
        } else {
          decode_resolve.valid = std::ranges::equal(info_hash_seq, io_view.first(20));
          io_view = io_view.subspan(20);
          if ( !decode_resolve.valid ) break;
          range = peer_id;
        }

      }

      if ( range == peer_id ) {

        if (io_view.size() < 20 ) {
          auto remaining = wraparound_steal(prepare, 20 - io_view.size());
          std::ranges::copy(io_view, peer_id_ref.begin());
          std::ranges::copy(remaining, peer_id_ref.begin() + io_view.size());
          break;
        } else {
          std::ranges::copy(io_view.first(20), peer_id_ref.begin());
          io_view = io_view.subspan(20);
          break;
        }

      }
    }

    buffer.commit_read(length::handshake);
    return decode_resolve;
  }

}

namespace keep_alive {

}

}

#endif
