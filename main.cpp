#include <iostream>
#include <fstream>
#include <vector>
#include "bencode.h"

int main() {
  std::ifstream file("hello.txt", std::ios::binary | std::ios::ate);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size)) {
    std::cerr << "Failed to read " << size << " bytes from the file." << std::endl;
    return 0;
  }
  auto start = buffer.cbegin();
  auto end = buffer.cend();
  BencodeObj *root = BencodeDecode(start, end);
  root->get(1)->print(std::cout);
  return 0;
}
