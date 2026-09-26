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
#include <string>
#include <variant>
#include <vector>

// NOTE; in cass of bug; changed function definition of decoders
// from ifstream& type paramaters to istream&

enum class Bendata_init_flag { integer, string, dictionary, list };
using ben_t = char;
constexpr ben_t BEN_DIC_T = 'd';
constexpr ben_t BEN_LIS_T = 'l';
constexpr ben_t BEN_NUM_T = 'i';
constexpr ben_t BEN_STR_T = 's';
constexpr ben_t BEN_DELIMETER = 'e';

struct BenDictPair {
  std::string key;
  std::string bencoded_value;
};

class Bendata {
private:
  std::variant<std::int64_t, std::string, std::vector<Bendata>,std::map<std::string, Bendata>> actual_value;
  std::string bencode;
  ben_t _t;
public:
  Bendata() = default;
  ~Bendata() = default;
  Bendata(std::int64_t number);
  Bendata(std::string string);
  explicit Bendata(Bendata_init_flag flag);

  template <typename T> T &get_data();
  template <typename T> const T &get_data() const;
  ben_t get_t() const;
  const std::string &get_encode() const;

  friend bool bendecode_integer(std::istream &, Bendata &);
  friend bool bendecode_string(std::istream &, Bendata &);
  friend bool bendecode_dictionary(std::istream &, Bendata &);
  friend bool bendecode_list(std::istream &, Bendata &);

  static std::string encode(std::int64_t);
  static std::string encode(const std::string&);
  static std::string encode_to_list(std::vector<std::string>);
  static std::string encode_to_dict(std::vector<BenDictPair>);

  friend std::ostream &operator<<(std::ostream &os, const Bendata &ben_object);
};

template <typename T> T &Bendata::get_data() {
  T &value = std::get<T>(actual_value);
  return value;
}

template <typename T>
const T &Bendata::get_data() const {
  const T &value = std::get<T>(actual_value);
  return value;
}

bool get_bendata_from_stream(std::istream &, Bendata &);
Bendata bendecode_from_file(std::istream &);

namespace ben {
using str = std::string;
using num = std::int64_t;
using lis = std::vector<Bendata>;
using dic = std::map<str, Bendata>;
} // namespace ben

#endif
