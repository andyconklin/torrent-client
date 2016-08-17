#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <openssl/sha.h>

#include "bencode.h"

std::vector<char> ReadFile(std::string path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size)) {
    throw std::logic_error("ReadFile failed");
  }
  return buffer;
}

int main(int argc, char* argv[]) {
  std::vector<char> metainfo = ReadFile(argv[1]);
  auto curr = metainfo.cbegin();
  BencodeObj *root = BencodeDecode(metainfo.cbegin(), curr, metainfo.cend());
  BencodeObj *info = root->get("info");

  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char *>(&metainfo[info->bounds.first]), 1 + info->bounds.second - info->bounds.first, hash);

  for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
    std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)(hash[i]);
  }
  std::cout << std::endl;
  return 0;
}
