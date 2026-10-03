#include "Bencode.hpp"
#include <cstdint>

Bendata::Bendata(std::int64_t number) : actual_value{number}, _t{bencode_type::integer} {}

Bendata::Bendata(std::string string) : actual_value{string}, _t{bencode_type::string} {}

Bendata::Bendata(bencode_type type) {

  switch (type) {
  case bencode_type::integer:     actual_value.emplace<bendecoded::integer>();    break;
  case bencode_type::string:      actual_value.emplace<bendecoded::string>();     break;
  case bencode_type::dictionary:  actual_value.emplace<bendecoded::dictionary>(); break;
  case bencode_type::list:        actual_value.emplace<bendecoded::list>();       break;
  }
  _t = type;

}

bencode_type Bendata::type() const { return _t; }

std::ostream &operator<<(std::ostream &os, const Bendata &ben_object) {
  switch (ben_object._t) {
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
  }
  return os;
}
