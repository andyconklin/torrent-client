#include <iostream>
#include <fstream>
#include <sstream>
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
#include <unistd.h>
#include <map>

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
        std::cout << "A new peer has connected." << std::endl;
      }
      for (auto it = connected.cbegin(); it != connected.cend(); it++) {
        if (FD_ISSET(*it, &fds)) {
          char sockbuf[1024];
          read(*it, sockbuf, 1024);
          std::cout << "FD=" << *it << " says: " << sockbuf << std::endl;
        }
      }
    }
  }
}

void BeginTorrentDownload(std::string path_to_torrent_file) {
  /* First, start listening */
  std::thread networker(NetworkerEntry);

  /* Next, initialize curl */
  CURL *curl = curl_easy_init();
  if (!curl) throw std::logic_error("curl_easy_init() failed");

  /* Read and decode the .torrent file */
  std::vector<char> metainfo = ReadFile(path_to_torrent_file);
  BencodeObj *root = BencodeDecode(metainfo); /* must be deleted later */

  /* Get the info dict */
  BencodeObj *info = root->get("info");

  /* Calculate info_hash */
  unsigned char info_hash[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char *>(&metainfo[info->bounds.first]),
      1 + info->bounds.second - info->bounds.first, info_hash);

  /* Randomly generate peer_id */
  unsigned char peer_id[20];
  RAND_bytes(peer_id, sizeof(peer_id));

  /* Calculate torrent size */
  int torrent_size;
  try {
    torrent_size = info->get("length")->get_int();
  } catch (std::exception &e) {
    torrent_size = 0;
    std::vector<BencodeObj *> list = info->get("files")->get_list();
    for (auto &obj : list) {
      torrent_size += obj->get("length")->get_int();
    }
  }

  /* URL encode the binary data */
  char *escaped_info_hash =
      curl_easy_escape(curl, reinterpret_cast<const char *>(info_hash), 20);
  char *escaped_peer_id =
      curl_easy_escape(curl, reinterpret_cast<const char *>(peer_id), 20);

  /* Generate URL of tracker request */
  std::ostringstream url;
  url << root->get("announce")->get_string();
  url << "?compact=0" << "&info_hash=" << escaped_info_hash << "&peer_id="
      << escaped_peer_id
      << "&port=6881&uploaded=0&downloaded=0&left=" << torrent_size;

  /* Make the request */
  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
  curl_easy_perform(curl);

  curl_free(escaped_info_hash);
  curl_free(escaped_peer_id);
  curl_easy_cleanup(curl);

  delete root;
  networker.join();
}

int main(int argc, char* argv[]) {
  if (argc != 2) return 0;
  BeginTorrentDownload(argv[1]);
  return 0;
}
