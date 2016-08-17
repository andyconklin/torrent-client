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
#include <sys/select.h>

#include "bencode.h"

#define MAX_CONNECTIONS 10

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
  /* http://stackoverflow.com/questions/2284428 */
  fd_set fds;
  struct timeval timeout;
  int max_fd = 0;
  std::vector<int> connected;

  int my_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (my_socket < 0) throw std::logic_error("Unable to init socket.");
  struct sockaddr_in my_address = {AF_INET, htons(6881),
      {htonl(INADDR_ANY)}, {0,0,0,0,0,0,0,0}};
  if (bind(my_socket, reinterpret_cast<const sockaddr *>(&my_address),
      sizeof(my_address)) != 0)
    throw std::logic_error("Unable to bind on port 6881.");
  if (listen(my_socket, MAX_CONNECTIONS) < 0)
    throw std::logic_error("Listen failed.");
  while (true) {
    FD_ZERO(&fds);
    FD_SET(my_socket, &fds);
    max_fd = (max_fd > my_socket) ? max_fd : my_socket + 1;
    for (auto it = connected.cbegin(); it != connected.cend(); it++) {
      FD_SET(*it, &fds);
      max_fd = (max_fd > *it) ? max_fd : *it + 1;
    }
    timeout.tv_sec = 2; timeout.tv_usec = 0;
    if (select(max_fd, &fds, NULL, NULL, &timeout) > 0) {
      if (FD_ISSET(my_socket, &fds)) {
        connected.push_back(accept(my_socket, NULL, NULL));
      }
      for (auto it = connected.cbegin(); it != connected.cend(); it++) {
        if (FD_ISSET(*it, &fds)) {
          /* peer has something to say */
        }
      }
    }
  }
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
