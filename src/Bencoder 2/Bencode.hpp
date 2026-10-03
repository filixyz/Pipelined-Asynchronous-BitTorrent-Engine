#ifndef BENCODE
#define BENCODE

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ben {

enum header: char {
  integer     = 'i',
  list        = 'l',
  dictionary  = 'd',
};

enum class encode_type {
  integer     = 0,
  string      = 1,
  list        = 2,
  dictionary  = 3,
  nothing     = 4,
};

constexpr char delimeter = 'e';

struct BenDictPair {
  std::string key;
  std::string bencoded_value;
};

class data;

namespace decoded_type {
  using integer     = typename std::int64_t;
  using string      = typename std::string;
  using list        = typename std::vector<data>;
  using dictionary  = typename std::map<std::string, data>;
}

struct source_t {

  std::string_view source;
  std::size_t cursor{0};

  auto undecoded() {
    return source.substr(cursor);
  }

};

struct source_stats_t {
  std::size_t start {0};
  std::size_t size  {0};
};

class data {

  std::variant <
    decoded_type::integer,
    decoded_type::string,
    decoded_type::list,
    decoded_type::dictionary
  > actual_value;


public:

  source_stats_t position_in_source{};

  data() = default;
  data(std::int64_t number);
  data(std::string string);
  explicit data(encode_type type);

  template <typename T> T &get_as() {
    return std::get<T>(actual_value);
  }

  template <typename T> const T &get_as() const {
    return std::get<T>(actual_value);
  }

  encode_type type() const;

  friend struct decoders;
  friend std::ostream &operator<<(std::ostream &os, const data &ben_object);

};

using decode_t = std::optional<data>;

struct decoders {
  static decode_t integer(source_t&);
  static decode_t string(source_t&);
  static decode_t dictionary(source_t&);
  static decode_t list(source_t&);
  static decode_t any(source_t&);
};

struct encoders {
  static std::string integer(std::int64_t);
  static std::string string(const std::string&);
  static std::string list(std::vector<std::string>);
  static std::string dictionary(std::vector<BenDictPair>);
};

// map containers lookup helper
inline auto* find(const decoded_type::dictionary::key_type& key, decoded_type::dictionary& map ) {
  auto it = map.find(key);
  return it == map.end() ? nullptr : &it->second;
}

}



#endif
