#include "Bencode.hpp"
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

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

decode_t decoders::string(source_t& encode) {

  decode_t decode{};

  std::size_t encode_begin = encode.cursor;

  auto view = encode.undecoded();

  if (view.size() < 2) return decode;

  auto seperator_index  = view.find_first_of(':');
  if (seperator_index == view.npos)
    return decode;

  auto length_string  = view.substr(0, seperator_index);
  auto string_begin   = seperator_index + 1;

  if (length_string.starts_with('-'))
    return decode;

  auto declared_size = get_number_if_valid(length_string);

  if (!declared_size ) return decode;

  if (auto remaining = view.size() - string_begin; declared_size > remaining)
    return decode;

  std::string_view actual_string = view.substr(string_begin, declared_size.value());

  bendecoded::string result {actual_string};

  encode.cursor += string_begin + declared_size.value();

  decode.result = std::move(result);
  decode.result.value().position_in_source.start = encode_begin;
  decode.result.value().position_in_source.size = encode.cursor - encode_begin;

  return decode;

}

decode_t decoders::integer(source_t& encoded) {

  decode_t decode;

  if (!encoded.undecoded().starts_with(header::integer))
    return decode;

  std::size_t encode_begin = encoded.cursor++;

  auto end_index = encoded.undecoded().find_first_of(bencode_delimeter);

  if (end_index == encoded.undecoded().npos)
    return decode;

  if (end_index == 0)
    return decode;

  auto number_string = encoded.undecoded().substr(0, end_index);

  auto decode_number = get_number_if_valid(number_string);
  if (!decode_number)
    return decode;

  encoded.cursor += end_index + 1;

  decode.result = decode_number.value();
  decode.result.value().position_in_source.start  = encode_begin;
  decode.result.value().position_in_source.size   = encoded.cursor - encode_begin;

  return decode;

}


decode_t decoders::any(source_t& encoded) {

  decode_t decode;

  if (encoded.undecoded().empty())
    return decode;

  auto header_type = encoded.undecoded().front();

  if (header_type == header::integer) {
    decode = decoders::integer(encoded);
    if (!decode.result)
      std::cerr << "integer decode failed\n";
  }
  else if ( header_type == header::list) {
    decode = decoders::list(encoded);
    if (!decode.result)
      std::cerr << "list decode failed\n";
  }
  else if ( header_type == header::dictionary) {
    decode = decoders::dictionary(encoded);
    if (!decode.result)
      std::cerr << "dictionary decode failed\n";
  }
  else {
    decode = decoders::string(encoded);
    if (!decode.result)
      std::cerr << "string decode failed\n";
  }

  return decode;
}

decode_t decoders::list(source_t& encode) {

  decode_t decode;

  if (auto undecoded = encode.undecoded(); undecoded.empty() or !undecoded.starts_with(header::list))
    return decode;

  std::size_t encode_begin = encode.cursor++;

  if (encode.undecoded().empty()) return decode;

  decode.result.emplace(bencode_type::list);

  bendecoded::list& list =
    decode.result.value().get_as<bendecoded::list>();

  while ( !encode.undecoded().empty() && encode.undecoded().front() != bencode_delimeter ) {

    auto bendecoded = decoders::any(encode);

    if (!bendecoded.result) {
      decode.result.reset();
      break;
    }

    list.push_back(std::move(bendecoded.result.value()));

  }

  if (!decode.result) /* decoded something wrong */ return decode;

  if ( encode.undecoded().empty()) {
    decode.result.reset();
    return decode;
  }

  if ( encode.undecoded().front() != bencode_delimeter ) {
    decode.result.reset();
    return decode;
  }

  encode.cursor+=1;

  decode.result.value().position_in_source.start = encode_begin;
  decode.result.value().position_in_source.size = encode.cursor - encode_begin;

  return decode;

}

decode_t decoders::dictionary(source_t& encode) {

  decode_t decode;

  if (auto undecoded = encode.undecoded(); undecoded.empty() or !undecoded.starts_with(header::dictionary))
    return decode;

  auto encode_begin = encode.cursor++;

  if (encode.undecoded().empty()) return decode;

  decode.result.emplace(bencode_type::dictionary);

  bendecoded::dictionary& dictionary=
    decode.result.value().get_as<bendecoded::dictionary>();

  while ( !encode.undecoded().empty() && encode.undecoded().front() != bencode_delimeter ) {

    auto bendecoded_key = decoders::string(encode);
    if (!bendecoded_key.result) {
      decode.result.reset();
      break;
    }

    auto bendecoded_val = decoders::any(encode);
    if (!bendecoded_val.result) {
      decode.result.reset();
      break;
    }

    auto key = std::move( bendecoded_key.result.value().get_as<bendecoded::string>() );
    auto val = std::move( bendecoded_val.result.value() );

    auto [it, unique] = dictionary.try_emplace(std::move(key), std::move(val));

    if (!unique) {
      decode.result.reset();
      break;
    }

  }

  if (!decode.result) /* decoded trash somewhere */ return decode;

  if ( encode.undecoded().empty()) {
    decode.result.reset();
    return decode;
  }

  if ( encode.undecoded().front() != bencode_delimeter ) {
    decode.result.reset();
    return decode;
  }

  encode.cursor+=1;

  decode.result.value().position_in_source.start = encode_begin;
  decode.result.value().position_in_source.size = encode.cursor - encode_begin;

  return decode;

}
