#include "../Errorhandlers/BittorentErrors.hpp"
#include "Bencode.hpp"
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <ios>
#include <string>

bool noexcept_stoi(std::string str, std::int64_t &result) {
  try {
    result = std::stoll(str);
  } catch (...) {
    return error_with_reason("noexcept_stoi failed");
  }
  return true;
}

bool bendecode_string(std::istream &of, Bendata &data) {
  char c;
  std::string size;
  while (of >> c && c != ':')
    size += c;
  std::int64_t size_int;
  if (!noexcept_stoi(size, size_int))
    return false;
  ben::str result;
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

bool bendecode_integer(std::istream &of, Bendata &data) {
  read_delimeter(of);
  std::string number_str;
  char c;
  while (of >> c && c != BEN_DELIMETER)
    number_str += c;
  std::int64_t value;
  if (!noexcept_stoi(number_str, value))
    return false;
  Bendata temp_int(value);
  data = std::move(temp_int);
  data.bencode = 'i' + std::to_string(value) + 'e';
  return true;
}

// peek_skipws returns-> ifstream::peek()
//    peeks into stream and if a whitsepace is there
//    it reads "skips" it by reading it and discarding
//    its value.
auto peek_skipws(std::istream &of) {
  while (std::iswspace(of.peek()))
    of.get(); // read that whitespace
  return of.peek();
}

bool get_bendata_from_stream(std::istream &of, Bendata &data) {
  switch (peek_skipws(of)) {
  case BEN_NUM_T:
    if (!bendecode_integer(of, data))
      return error_with_reason("bendecode_integer failed");
    break;
  case BEN_DIC_T:
    if (!bendecode_dictionary(of, data))
      return error_with_reason("bendecode_dictionary failed");
    break;
  case BEN_LIS_T:
    if (!bendecode_list(of, data))
      return error_with_reason("bendecode_list failed");
    break;
  default:
    if (!bendecode_string(of, data))
      return error_with_reason("bendecode_string failed");
    break;
  }
  return true;
}

bool bendecode_list(std::istream &of, Bendata &data) {
  Bendata new_list{Bendata_init_flag::list};
  ben::lis &ref_list = new_list.get_data<ben::lis>();
  new_list.bencode += read_delimeter(of);
  while (peek_skipws(of) != BEN_DELIMETER) {
    Bendata new_data{};
    if (!get_bendata_from_stream(of, new_data))
      return false;
    ref_list.push_back(new_data);
    new_list.bencode += new_data.bencode;
  }
  new_list.bencode += read_delimeter(of);
  data = std::move(new_list);
  return true;
}

bool get_benkey_from_stream(std::istream &of, Bendata &key) {
  if (!bendecode_string(of, key))
    return error_with_reason("get_benkey failed: string");
  return true;
}

bool bendecode_dictionary(std::istream &of, Bendata &data) {
  Bendata new_dict{Bendata_init_flag::dictionary};
  ben::dic &ref_dic = new_dict.get_data<ben::dic>();
  new_dict.bencode += read_delimeter(of);
  while (peek_skipws(of) != BEN_DELIMETER) {
    Bendata new_key{};
    Bendata new_data{};
    if (!get_benkey_from_stream(of, new_key))
      return false;
    if (!get_bendata_from_stream(of, new_data))
      return false;
    ref_dic[new_key.get_data<std::string>()] = new_data;
    new_dict.bencode += new_key.bencode + new_data.bencode;
  }
  new_dict.bencode += read_delimeter(of);
  data = std::move(new_dict);
  return true;
}

Bendata bendecode_from_file(std::istream &file) {
  file >> std::noskipws;
  Bendata parsed;
  if (!get_bendata_from_stream(file, parsed))
    throw Invalid_Bencode_File{};
  return parsed;
}
