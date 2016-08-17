#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <curl/curl.h>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

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

void NetworkerEntry() {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) throw std::logic_error("Unable to init socket.");
  struct sockaddr_in my_address = {AF_INET, htons(6881), {htonl(INADDR_ANY)}, {0,0,0,0,0,0,0,0}};
  int bindres = bind(sockfd, reinterpret_cast<const sockaddr *>(&my_address), sizeof(my_address));
  if (bindres != 0) throw std::logic_error("Unable to bind on port 6881.");
  std::cout << SOMAXCONN << std::endl;
}

int main(int argc, char* argv[]) {
  std::vector<char> metainfo = ReadFile(argv[1]);
  auto curr = metainfo.cbegin();
  BencodeObj *root = BencodeDecode(metainfo.cbegin(), curr, metainfo.cend());
  BencodeObj *info = root->get("info");

  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char *>(&metainfo[info->bounds.first]), 1 + info->bounds.second - info->bounds.first, hash);

  CURL *curl = curl_easy_init();
  if (!curl) throw std::logic_error("curl_easy_init() failed");

  char *escaped = curl_easy_escape(curl, reinterpret_cast<const char *>(hash), 20);
  std::cout << escaped << std::endl;
  curl_free(escaped);

  unsigned char randbytes[20];
  RAND_bytes(randbytes, sizeof(randbytes));

  escaped = curl_easy_escape(curl, reinterpret_cast<const char *>(randbytes), 20);
  std::cout << escaped << std::endl;
  curl_free(escaped);

  curl_easy_cleanup(curl);

  std::thread networker(NetworkerEntry);
  networker.join();

  return 0;
}
