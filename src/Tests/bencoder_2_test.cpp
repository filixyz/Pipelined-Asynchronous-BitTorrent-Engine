#include "../Bencoder 2/Bencode.hpp"
#include <fstream>
#include <ios>
#include "../Client/Hasher.hpp"

int main(int argc,  char* argv[]) {

  if (argc != 2) { std::cerr << "give a metainfo file\n"; return 1; }

  std::ifstream file (argv[1], std::ios::in | std::ios::binary | std::ios::ate);

  if (!file.is_open()) std::cerr << "file failed to oper\n";

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::string bencode_buffer;
  bencode_buffer.resize(size);

  auto& read_success = file.read(&bencode_buffer[0], size);

  if (!read_success)  std::cerr << "buffer store failed\n";

  ben::source_t encoded_source { .source=bencode_buffer, .cursor=0 };
  auto decode = ben::decoders::any(encoded_source);

  if (decode) std::cout << "decode successful\n";
  else        std::cout << "decode failed\n";

  ben::data* found_value;
  if  ( !(found_value = ben::find("info", decode.value().get_as<ben::decoded_type::dictionary>())) )
    std::cerr << "info key not found\n";

  auto& info_data = *found_value;

  const auto sha1_hash = Hasher::get_sha1 (
    { reinterpret_cast<const std::byte*>(&bencode_buffer[info_data.position_in_source.start]), info_data.position_in_source.size }
  );

  std::cout << "Hash: " << Hasher::hex_stringify_hash(std::span<const std::byte>(sha1_hash)) << '\n';

}
