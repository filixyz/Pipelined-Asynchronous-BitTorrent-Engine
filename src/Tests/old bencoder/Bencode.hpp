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

  std::variant<std::int64_t, std::string, std::vector<Bendata>,std::map<std::string, Bendata>> actual_value;
  std::string bencode;
  bencode_type _t;

public:

  Bendata() = default;
  Bendata(std::int64_t number);
  Bendata(std::string string);
  explicit Bendata(bencode_type type);

  template <typename T> T &get_as();
  template <typename T> const T &get_as() const;
  bencode_type type() const;
  const std::string &get_encode() const;

  static bool decode_integer(std::istream &, Bendata &);
  static bool decode_string(std::istream &, Bendata &);
  static bool decode_dictionary(std::istream &, Bendata &);
  static bool decode_list(std::istream &, Bendata &);
  static std::string encode(std::int64_t);
  static std::string encode(const std::string&);
  static std::string encode_to_list(std::vector<std::string>);
  static std::string encode_to_dict(std::vector<BenDictPair>);

  friend std::ostream &operator<<(std::ostream &os, const Bendata &ben_object);
};

template <typename T> T &Bendata::get_as() {
  T &value = std::get<T>(actual_value);
  return value;
}

template <typename T> const T &Bendata::get_as() const {
  const T &value = std::get<T>(actual_value);
  return value;
}

std::optional<Bendata> bendecode(std::istream &);

// map containers lookup helper
template<class M> auto* find(const typename M::key_type& key, M& map ) {
  auto it = map.find(key);
  return it == map.end() ? nullptr : &it->second;
}

#endif
