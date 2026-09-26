#include "Bencode.hpp"
#include <cstdint>
#include <string>
#include <algorithm>

std::string Bendata::encode(std::int64_t value){
  return std::string{'i'} + std::to_string(value) + 'e';
}
std::string Bendata::encode(const std::string& value){
  return std::string{} + std::to_string(value.length()) + ':' + value;
}
std::string Bendata::encode_to_list(std::vector<std::string> items) {
  std::string res{'l'};
  for (auto& i : items) res+=i;
  return res += 'e';
}
std::string Bendata::encode_to_dict(std::vector<BenDictPair> items) {
  std::sort(items.begin(), items.end(),
    [](BenDictPair& a, BenDictPair& b){ return a.key < b.key; });
  std::string encoded_result("d");
  for (auto& pair : items) encoded_result += encode(pair.key) + pair.bencoded_value;
  encoded_result += "e";
  return encoded_result;
}
