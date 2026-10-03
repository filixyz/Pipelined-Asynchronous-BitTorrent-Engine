#include "../Errorhandlers/BittorentErrors.hpp"
#include "Bencode.hpp"
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <ios>
#include <optional>
#include <string>

bool noexcept_stoi(std::string str, std::int64_t &result) {
  try {
    result = std::stoll(str); // replace with std::from_chars later
  } catch (...) {;
    return error_with_reason("noexcept_stoi failed");
  }
  return true;
}

bool Bendata::decode_string(std::istream &of, Bendata &data) {

  char c;
  std::string size;
  while (of >> c && c != ':') {
    size += c;
  }

  std::int64_t size_int;
  if (!noexcept_stoi(size, size_int))
    return false;
  bendecoded::string result;
  while (size_int-- > 0) {
    of >> c;
    result += c;
  }
  Bendata temp_str(result);
  data = std::move(temp_str);
  data.bencode = size + ':' + result;
  return true;
}

// Reads an expected bencoder delimeter
// then discards it
inline char read_delimeter(std::istream &of) {
  char c;
  of >> c;
  return c;
}

bool Bendata::decode_integer(std::istream &of, Bendata &data) {
  read_delimeter(of);
  std::string number_str;
  char c;
  while (of >> c && c != bencode_delimeter)
    number_str += c;
  std::int64_t value;
  if (!noexcept_stoi(number_str, value))
    return false;
  Bendata temp_int(value);
  data = std::move(temp_int);
  data.bencode = 'i' + std::to_string(value) + 'e';
  return true;
}


bool get_bendata_from_stream(std::istream &of, Bendata &data) {

  switch (of.peek()) {

  case bencode_type::integer:
    if (!Bendata::decode_integer(of, data))     return error_with_reason("bendecode_integer failed");    break;
  case bencode_type::dictionary:
    if (!Bendata::decode_dictionary(of, data))  return error_with_reason("bendecode_dictionary failed"); break;
  case bencode_type::list:
    if (!Bendata::decode_list(of, data))        return error_with_reason("bendecode_list failed");       break;
  default:
    if (!Bendata::decode_string(of, data))      return error_with_reason("bendecode_string failed");     break;

  }
  return true;

}

bool Bendata::decode_list(std::istream &of, Bendata &data) {
  Bendata new_list {bencode_type::list};
  bendecoded::list &ref_list = new_list.get_as<bendecoded::list>();
  new_list.bencode += read_delimeter(of);
  while (of.peek() != bencode_delimeter) {
    Bendata new_data{};
    if (!get_bendata_from_stream(of, new_data))
      return false;
    new_list.bencode += new_data.bencode;
    ref_list.push_back(std::move(new_data));
  }
  new_list.bencode += read_delimeter(of);
  data = std::move(new_list);
  return true;
}

bool get_benkey_from_stream(std::istream &of, Bendata &key) {
  if (!Bendata::decode_string(of, key))
    return error_with_reason("get_benkey failed: string");
  return true;
}

bool Bendata::decode_dictionary(std::istream &of, Bendata &data) {
  Bendata new_dict {bencode_type::dictionary};
  bendecoded::dictionary &ref_dic = new_dict.get_as<bendecoded::dictionary>();
  new_dict.bencode += read_delimeter(of);
  while (of.peek() != bencode_delimeter) {
    Bendata new_key{};
    Bendata new_data{};
    if (!get_benkey_from_stream(of, new_key))
      return false;
    if (!get_bendata_from_stream(of, new_data))
      return false;
    new_dict.bencode += new_key.bencode + new_data.bencode;
    ref_dic[new_key.get_as<bendecoded::string>()] = std::move(new_data);
  }
  new_dict.bencode += read_delimeter(of);
  data = std::move(new_dict);
  return true;
}

std::optional<Bendata> bendecode(std::istream &file) {

  file >> std::noskipws;
  Bendata parsed;
  if ( get_bendata_from_stream(file, parsed) ) return parsed;
  return std::nullopt;

}
