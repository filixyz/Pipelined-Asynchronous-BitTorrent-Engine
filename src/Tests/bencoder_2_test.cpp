#include "../Bencode/Ben.hpp"
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

  std::cout << "file size: " << size << " bytes\n";

  auto& read_success = file.read(&bencode_buffer[0], size);

  if (!read_success) std::cerr << "buffer store failed\n";

  auto decode = ben::decode::input(bencode_buffer);

  if (decode)
    std::cout << "decode successful\n";
  else  {
    std::cerr << "decode failed\n";
    return 1;
  }

  const ben::data* found_value;
  if ( !(found_value = ben::find("info", decode.value().get_as<ben::decoded_type::dictionary>())) ) {
    std::cerr << "info key not found\n";
    return 1;
  }

  auto& info_data = *found_value;

  auto pos_in_src = info_data.get_position_in_source();
  const auto sha1_hash = Hasher::get_sha1 (
    { reinterpret_cast<const std::byte*>(&bencode_buffer[pos_in_src.start]), pos_in_src.size }
  );

  std::cout << "Info Hash: " << Hasher::hex_stringify_hash(std::span<const std::byte>(sha1_hash)) << '\n';

}
