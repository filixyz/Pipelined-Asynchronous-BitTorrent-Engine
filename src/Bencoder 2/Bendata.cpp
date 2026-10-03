#include "Bencode.hpp"
#include <cstdint>
#include <variant>

namespace ben {

data::data(std::int64_t number) : actual_value{number} {}

data::data(std::string string) : actual_value{std::move(string)} {}

data::data(encode_type type) {

  switch (type) {
  case encode_type::integer:     actual_value.emplace<decoded_type::integer>();    break;
  case encode_type::string:      actual_value.emplace<decoded_type::string>();     break;
  case encode_type::list:        actual_value.emplace<decoded_type::list>();       break;
  case encode_type::dictionary:  actual_value.emplace<decoded_type::dictionary>(); break;
  case encode_type::nothing:     break;
  }

}

encode_type data::type() const {

  if (actual_value.index() == 0)  return encode_type::integer;
  if (actual_value.index() == 1)  return encode_type::string;
  if (actual_value.index() == 2)  return encode_type::list;
  if (actual_value.index() == 3)  return encode_type::dictionary;

  return encode_type::nothing;

}

std::ostream &operator<<(std::ostream &os, const data &ben_object) {

  switch (ben_object.type()) {

  case encode_type::string: {
    os << '\"' << std::get<std::string>(ben_object.actual_value) << '\"';
    break;
  }

  case encode_type::integer: {
    os << std::get<std::int64_t>(ben_object.actual_value);
    break;
  }

  case encode_type::list: {
    const std::vector<data> &list =
        ben_object.get_as<decoded_type::list>();
    os << '[';
    for (size_t i = 0; i < list.size(); ++i) {
      if (i == list.size() - 1)
        os << list[i];
      else
        os << list[i] << ", ";
    }
    os << ']';
    break;
  }

  case encode_type::dictionary: {
    const std::map<std::string, data> &dicts =
        ben_object.get_as<decoded_type::dictionary>();
    std::size_t d_size = dicts.size(), index = 0;
    os << '{';
    for (const auto &x : dicts) {
      ++index;
      if (index != d_size)
        os << x.first << " : " << x.second << ", ";
      else
        os << x.first << " : " << x.second;
    }
    os << '}';
  }

  case encode_type::nothing:  return os; break;

  }
  return os;
}

}
