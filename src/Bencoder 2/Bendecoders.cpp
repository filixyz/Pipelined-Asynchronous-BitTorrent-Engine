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

  constexpr int base = 10;

  if (str.front() == '0' and str.size() != 1) // handle trailing zeros
    return std::nullopt;

  std::int64_t decode{};
  auto valid_end = str.data() + str.size();
  auto [ptr, err] = std::from_chars(str.data(), valid_end, decode, base);

  if (err == std::errc() && ptr == valid_end)
    return decode;

  return std::nullopt;

}

decode_t decoders::string(std::string_view view) {

  decode_t decode{};

  if (view.size() < 2) return decode;

  auto seperator_index  = view.find_first_of(':');
  if (seperator_index == view.npos)
    return decode;

  auto length_string  = view.substr(0, seperator_index);
  auto string_begin   = seperator_index + 1;

  std::string_view actual_string;
  if (string_begin < view.size())
    actual_string = view.substr(string_begin);

  auto string_size = get_number_if_valid(length_string);

  if (!string_size) return decode;

  if (static_cast<std::size_t>(string_size.value()) != actual_string.length())
    return decode;

  bendecoded::string result {actual_string};

  decode.length = string_begin + string_size.value();
  decode.result = result;

  return decode;

}


decode_t decoders::integer(std::string_view view) {

  decode_t decode;

  if (!view.starts_with(bencode_type::integer))
    return decode;

  auto start_index = std::size_t(0);
  auto end_index = view.find_first_of(bencode_delimeter);

  if (end_index == view.npos)
    return decode;

  if (start_index+1 == end_index)
    return decode;

  auto number_string = view.substr(start_index+1, end_index);

  auto decode_number = get_number_if_valid(number_string);
  if (!decode_number)
    return decode;

  decode.length = end_index + 1;
  decode.result = decode_number.value();

  return decode;

}


decode_t decoders::any(std::string_view view) {

  decode_t decode;

  if (view.empty())
    return decode;

  if (view.front() == bencode_type::integer) {
    decode = decoders::integer(view);
    if (!decode.result)
      std::cerr << "integer decode failed\n";
  }
  else if (view.front() == bencode_type::list) {
    decode = decoders::list(view);
    if (!decode.result)
      std::cerr << "list decode failed\n";
  }
  else if (view.front() == bencode_type::dictionary) {
    decode = decoders::dictionary(view);
    if (!decode.result)
      std::cerr << "dictionary decode failed\n";
  }
  else {
    decode = decoders::string(view);
    if (!decode.result)
      std::cerr << "string decode failed\n";
  }

  return decode;
}

decode_t decoders::list(std::string_view view) {

  decode_t decode;

  if (!view.starts_with(bencode_type::list)) return decode;

  decode.result.emplace(bencode_type::list);

  auto list_start = view.substr(1);

  bendecoded::list& list =
    decode.result.value().get_as<bendecoded::list>();

  while ( list_start.front() != bencode_delimeter) {

    if (list_start.size() == 1) {
      decode.result.reset();
      break;
    }

    auto bendecoded = decoders::any(list_start);

    list_start = list_start.substr(bendecoded.length);
    decode.length += bendecoded.length;

    if (!bendecoded.result) {
      decode.result.reset();
      break;
    }

    list.push_back(std::move(bendecoded.result.value()));

  }

  if (!decode.result) /* decoded something wrong */ return decode;

  decode.length += 2; /* including its delimter and character header */

  return decode;

}

decode_t decoders::dictionary(std::string_view view) {

  decode_t decode;

  if (!view.starts_with(bencode_type::dictionary))  return decode;

  decode.result.emplace(bencode_type::dictionary);

  auto dict_start = view.substr(1);

  bendecoded::dictionary& dictionary=
    decode.result.value().get_as<bendecoded::dictionary>();

  while ( dict_start.front() != bencode_delimeter ) {

    if (dict_start.size() == 1) {
      decode.result.reset();
      break;
    }

    auto bendecoded_key = decoders::string(dict_start);
    if (!bendecoded_key.result) {
      decode.result.reset();
      break;
    }

    dict_start = dict_start.substr(bendecoded_key.length);
    decode.length += bendecoded_key.length;

    auto bendecoded_val = decoders::any(dict_start);
    if (!bendecoded_val.result) {
      decode.result.reset();
      break;
    }

    dict_start = dict_start.substr(bendecoded_val.length);
    decode.length += bendecoded_val.length;

    auto key = bendecoded_key.result.value().get_as<bendecoded::string>();
    auto val = bendecoded_val.result.value();

    auto [it, unique] = dictionary.try_emplace(key, std::move(val));

    if (!unique) {
      decode.result.reset();
      break;
    }

  }

  if (!decode.result) /* decoded trash somewhere */ return decode;

  decode.length +=2; // including its delimeter and character header.

  return decode;

}
