#include "Ben.hpp"
#include <charconv>
#include <optional>
#include <system_error>

// NOte: This decoders work (according to my tests) but a malicious input
// can blow the process stack if it contains dictinarys or lists with
// very deep nests, add a limit death cap later.
//
// man i'm tired boss.

namespace ben  {

namespace {

  std::optional<std::int64_t> get_number_if_valid(std::string_view str) {

    if (str.empty()) return std::nullopt;

    constexpr int base = 10;

    if (str.front() == '0' and str.size() != 1) // handle trailing zeros
      return std::nullopt;

    if (str.size() >= 2 and str.starts_with("-0"))
      return std::nullopt;

    std::int64_t decode{};
    auto valid_end = str.data() + str.size();
    auto [ptr, err] = std::from_chars(str.data(), valid_end, decode, base);

    if (err == std::errc() && ptr == valid_end)
      return decode;

    return std::nullopt;

  }

}

decode_t decode::string(source_t& encode) {

  decode_t resolve {};

  std::size_t encode_begin = encode.cursor;

  auto view = encode.undecoded();

  if (view.size() < 2) return resolve;

  auto seperator_index  = view.find_first_of(':');
  if (seperator_index == view.npos)
    return resolve;

  auto length_string  = view.substr(0, seperator_index);
  auto string_begin   = seperator_index + 1;

  if (length_string.starts_with('-'))
    return resolve;

  auto declared_size = get_number_if_valid(length_string);

  if (!declared_size ) return resolve;

  // chech if undecoded remaining in stream is enough for decode to actually
  // extract the declared number of bytes this string encode claims to have

  if (auto remaining = view.size() - string_begin; declared_size > remaining) // if buggy check >= here: >= was a bug
    return resolve;

  std::string_view actual_string = view.substr(string_begin, declared_size.value());

  decoded_type::string result {actual_string};

  encode.cursor += string_begin + declared_size.value();

  resolve = std::move(result);
  resolve.value().position_in_source.start = encode_begin;
  resolve.value().position_in_source.size = encode.cursor - encode_begin;

  return resolve;

}

decode_t decode::integer(source_t& encoded) {

  decode_t resolve;

  if (!encoded.undecoded().starts_with(header::integer))
    return resolve;

  std::size_t encode_begin = encoded.cursor++;

  auto end_index = encoded.undecoded().find_first_of(delimeter);

  if (end_index == encoded.undecoded().npos)
    return resolve;

  if (end_index == 0)
    return resolve;

  auto number_string = encoded.undecoded().substr(0, end_index);

  auto decode_number = get_number_if_valid(number_string);
  if (!decode_number)
    return resolve;

  encoded.cursor += end_index + 1;

  resolve = decode_number.value();
  resolve.value().position_in_source.start  = encode_begin;
  resolve.value().position_in_source.size   = encoded.cursor - encode_begin;

  return resolve;

}


decode_t decode::any(source_t& encoded) {

  if (encoded.undecoded().empty()) return std::nullopt;

  switch (encoded.undecoded().front()) {
    case header::integer:       return decode::integer(encoded);
    case header::list:          return decode::list(encoded);
    case header::dictionary:    return decode::dictionary(encoded);
    default:                    return decode::string(encoded);
  }

}

decode_t decode::list(source_t& encode) {

  decode_t resolve;

  if (auto undecoded = encode.undecoded(); undecoded.empty() or !undecoded.starts_with(header::list))
    return resolve;

  std::size_t encode_begin = encode.cursor++; // read header

  if (encode.undecoded().empty()) return resolve;

  resolve.emplace(encode_type::list);

  decoded_type::list& list =
    resolve.value().get_as<decoded_type::list>();

  while ( !encode.undecoded().empty() && encode.undecoded().front() != delimeter ) {

    auto bendecoded = decode::any(encode);

    if (!bendecoded) {
      resolve.reset();
      break;
    }

    list.push_back(std::move(bendecoded.value()));

  }

  if (!resolve) /* decoded something wrong */ return resolve;

  if ( encode.undecoded().empty()) {
    resolve.reset();
    return resolve;
  }

  if ( encode.undecoded().front() != delimeter ) {
    resolve.reset();
    return resolve;
  }

  encode.cursor+=1; // read delimeter

  resolve.value().position_in_source.start = encode_begin;
  resolve.value().position_in_source.size = encode.cursor - encode_begin;

  return resolve;

}

decode_t decode::dictionary(source_t& encode) {

  decode_t resolve;

  if (auto undecoded = encode.undecoded(); undecoded.empty() or !undecoded.starts_with(header::dictionary))
    return resolve;

  auto encode_begin = encode.cursor++; // read header

  if (encode.undecoded().empty()) return resolve;

  resolve.emplace(encode_type::dictionary);

  decoded_type::dictionary& dictionary=
    resolve.value().get_as<decoded_type::dictionary>();

  while ( !encode.undecoded().empty() && encode.undecoded().front() != delimeter ) {

    auto bendecoded_key = decode::string(encode);
    if (!bendecoded_key) {
      resolve.reset();
      break;
    }

    auto bendecoded_val = decode::any(encode);
    if (!bendecoded_val) {
      resolve.reset();
      break;
    }

    auto key = std::move( bendecoded_key.value().get_as<decoded_type::string>() );
    auto val = std::move( bendecoded_val.value() );

    auto [it, unique] = dictionary.try_emplace(std::move(key), std::move(val));

    if (!unique) {
      resolve.reset();
      break;
    }

  }

  if (!resolve) /* decoded trash somewhere */ return resolve;

  if ( encode.undecoded().empty()) {
    resolve.reset();
    return resolve;
  }

  if ( encode.undecoded().front() != delimeter ) {
    resolve.reset();
    return resolve;
  }

  encode.cursor+=1; // read delimeter

  resolve.value().position_in_source.start = encode_begin;
  resolve.value().position_in_source.size = encode.cursor - encode_begin;

  return resolve;

}

decode_t decode::input(std::string_view undecoded) {

  source_t source  = { .source=undecoded, .cursor=0 };
  decode_t resolve = decode::any(source);

  if (resolve && source.cursor != undecoded.size()) // there was trailing junk prefixed
    resolve.reset();

  return resolve;

}

}
