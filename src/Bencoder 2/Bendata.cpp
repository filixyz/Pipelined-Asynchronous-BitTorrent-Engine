#include "Bencode.hpp"
#include <cstdint>
#include <variant>

Bendata::Bendata(std::int64_t number) : actual_value{number}  {}

Bendata::Bendata(std::string string) : actual_value{std::move(string)}   {}

Bendata::Bendata(bencode_type type) {

  switch (type) {
  case bencode_type::integer:     actual_value.emplace<bendecoded::integer>();    break;
  case bencode_type::string:      actual_value.emplace<bendecoded::string>();     break;
  case bencode_type::list:        actual_value.emplace<bendecoded::list>();       break;
  case bencode_type::dictionary:  actual_value.emplace<bendecoded::dictionary>(); break;
  case bencode_type::nothing:     break;
  }

}

bencode_type Bendata::type() const {

  if (actual_value.index() == 0)  return bencode_type::integer;
  if (actual_value.index() == 1)  return bencode_type::string;
  if (actual_value.index() == 2)  return bencode_type::list;
  if (actual_value.index() == 3)  return bencode_type::dictionary;

  return bencode_type::nothing;
}

std::ostream &operator<<(std::ostream &os, const Bendata &ben_object) {
  switch (ben_object.type()) {
  case bencode_type::string: {
    os << '\"' << std::get<std::string>(ben_object.actual_value) << '\"';
    break;
  }
  case bencode_type::integer: {
    os << std::get<std::int64_t>(ben_object.actual_value);
    break;
  }
  case bencode_type::list: {
    const std::vector<Bendata> &list =
        ben_object.get_as<bendecoded::list>();
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
  case bencode_type::dictionary: {
    const std::map<std::string, Bendata> &dicts =
        ben_object.get_as<bendecoded::dictionary>();
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
  case bencode_type::nothing:
    return os;
  }
  return os;
}
