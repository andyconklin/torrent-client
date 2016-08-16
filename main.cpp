#include <iostream>
#include <fstream>
#include <vector>
#include "bencode.h"

int main() {
  std::ifstream file("hello.txt", std::ios::binary | std::ios::ate);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (file.read(buffer.data(), size))
  {
    auto start = buffer.cbegin();
    auto end = buffer.cend();
    BencodeDecode(start, end)->print(std::cout);
  }
  return 0;
}
