#ifndef BENCODE
#define BENCODE

// Bencode will be the library that provides facilities to interact
// with/represent Bee-data From a metainfo File to actual objects we can
// manipulate in code
//
// Classes:
//    Bendata -> An abstraction fo an actual bee-data instance
//               can be of underlying type integer, string, dictionary or list,
//               Each instance corresponds to a single constant underlying type,
//               E.G: once an integer alway an inetger.
// Functions:
//    Bendecode_integer -> overload 2
//        1) One for reading data directly from some file stream and converting
//        it to bee-data 2) One for converting some string to its appropriat
//        bee-data represenation
//    Bendecode_string -> overload 2 (same motive as previous function but for
//    strings) Bendecode_dictionary -> overload 2 (same motive as previous
//    function but for dictionaries) Bendecode_list -> overload 2 (same motive
//    as previous function but for lists)

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum bencode_type: char {
  string      = 's',
  integer     = 'i',
  list        = 'l',
  dictionary  = 'd',
};

constexpr char bencode_delimeter = 'e';

struct BenDictPair {
  std::string key;
  std::string bencoded_value;
};

class Bendata;

namespace bendecoded {
  using string      = typename std::string;
  using integer     = typename std::int64_t;
  using list        = typename std::vector<Bendata>;
  using dictionary  = typename std::map<std::string, Bendata>;
}


class Bendata {

  std::variant <
    std::int64_t, std::string, std::vector<Bendata>, std::map<std::string, Bendata>
  > actual_value;

  bencode_type _t;

public:

  Bendata() = default;
  Bendata(std::int64_t number);
  Bendata(std::string string);
  explicit Bendata(bencode_type type);

  template <typename T> T &get_as() {
    return std::get<T>(actual_value);
  }

  template <typename T> const T &get_as() const {
    return std::get<T>(actual_value);
  }

  bencode_type type() const;

  friend struct decoders;

  friend std::ostream &operator<<(std::ostream &os, const Bendata &ben_object);
};


struct decode_t {
  std::optional<Bendata> result;
  std::size_t length{0};
};

struct decoders {
  static decode_t integer(std::string_view);
  static decode_t string(std::string_view);
  static decode_t dictionary(std::string_view);
  static decode_t list(std::string_view);
  static decode_t any(std::string_view);
};

struct encoders {
  static std::string integer(std::int64_t);
  static std::string string(const std::string&);
  static std::string list(std::vector<std::string>);
  static std::string dictionary(std::vector<BenDictPair>);
};

// map containers lookup helper
template<class M> auto* find(const typename M::key_type& key, M& map ) {
  auto it = map.find(key);
  return it == map.end() ? nullptr : &it->second;
}

#endif
