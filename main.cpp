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
#include <fcntl.h>
#include <map>
#include <ctime>
#include <cstring>
#include <arpa/inet.h>

#include <cerrno>

#include "bencode.h"

#define STOP_CONNECTING 30
#define STOP_ACCEPTING 55

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

struct Peer {
  Peer(int fd, bool initiated) : fd(fd), received_from(std::time(NULL)), am_interested(false),
      am_choked(true), is_interested(false), is_choked(true), I_initiated(initiated) { }
  void load_info_hash(unsigned char *x) {
    for (int i = 0; i < 20; i++) info_hash[i] = x[i];
  }
  void load_peer_id(unsigned char *x) {
    for (int i = 0; i < 20; i++) peer_id[i] = x[i];
  }
  int fd;
  std::vector<bool> bitfield;
  std::time_t received_from;
  std::time_t sent_to;
  unsigned char info_hash[20];
  unsigned char peer_id[20];
  bool am_interested;
  bool am_choked;
  bool is_interested;
  bool is_choked;
  bool have_sent_handshake;
  bool have_received_handshake;
  bool I_initiated;
};

struct Torrent {
  BencodeObj *metainfo;
  BencodeObj *response;
  int peer_index;
  Torrent(BencodeObj* metainfo, BencodeObj* response) : metainfo(metainfo), response(response), peer_index(0) { }
  struct sockaddr_in yield_peer() {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

    std::string peers = response->get("peers")->get_string();
    std::cout << "HOST ";
    for (int i = 0; i < 4; i++) {
      reinterpret_cast<char*>(&(addr.sin_addr))[i] = peers.at(peer_index*6 + i);
      std::cout << (int)(unsigned char)(peers.at(peer_index*6 + i)) << " ";
    }
    std::cout << "PORT ";
    for (int i = 4; i < 6; i++) {
      reinterpret_cast<char*>(&(addr.sin_port))[i-4] = peers.at(peer_index*6 + i);
      std::cout << (int)(unsigned char)(peers.at(peer_index*6 + i)) << " ";
    }
    std::cout << std::endl;

    peer_index++;
    return addr;
  }
};

void NetworkerEntry(std::vector<Torrent> *torrents) {
  /* http://stackoverflow.com/questions/2284428 */
  fd_set readfds, writefds;
  struct timeval timeout;
  int max_fd = 0;
  std::vector<Peer> connected;

  int in_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (in_socket < 0) throw std::logic_error("Unable to init socket.");
  struct sockaddr_in my_address = {AF_INET, htons(6881),
      {htonl(INADDR_ANY)}, {0,0,0,0,0,0,0,0}};
  if (bind(in_socket, reinterpret_cast<const sockaddr *>(&my_address),
      sizeof(my_address)) != 0)
    throw std::logic_error("Unable to bind on port 6881.");
  if (listen(in_socket, 20) < 0)
    throw std::logic_error("Listen failed.");

  /* Networker loop: */
  while (true) {
    /* Cull the peers who have timed out. */
    for (auto it = connected.begin(); it != connected.end(); it++) {
      if (std::difftime(std::time(NULL), it->received_from) > 125) {
        close(it->fd);
        it = connected.erase(it);
        if (it != connected.begin()) it--;
      }
    }

    /* If we have under STOP_CONNECTING connections,
       then we should connect to a peer or several. */
    if (connected.size() < 1) {
      int new_sock = socket(AF_INET, SOCK_STREAM, 0);
      while (torrents->size() < 1);
      sockaddr_in addr = torrents->at(0).yield_peer();
      char checkbuf[300];
      inet_ntop(AF_INET, &(addr.sin_addr), checkbuf, sizeof(checkbuf));
      std::cout << "Connecting to " << checkbuf << ":" << ntohs(addr.sin_port) << "... ";
      int res = connect(new_sock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
      if (res == 0) {
        std::cout << "success!" << std::endl;
        connected.push_back(Peer(new_sock, true));
      }
      else std::cout << "failed. (" << std::strerror(errno) << ")" << std::endl;
    }

    /* Check the listening socket for new connections */
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_SET(in_socket, &readfds);
    max_fd = (max_fd > in_socket) ? max_fd : in_socket + 1;

    /* Check the existing connections */
    for (auto it = connected.cbegin(); it != connected.cend(); it++) {
      FD_SET(it->fd, &readfds);
      FD_SET(it->fd, &writefds);
      max_fd = (max_fd > it->fd) ? max_fd : it->fd + 1;
    }

    /* Spend 2 seconds looking for changes in all the connections */
    timeout.tv_sec = 2; timeout.tv_usec = 0;
    if (select(max_fd, &readfds, &writefds, NULL, &timeout) > 0) {
      /* Accept a new connection from the listening socket */
      if (FD_ISSET(in_socket, &readfds)) {
        connected.push_back(Peer(accept(in_socket, NULL, NULL), false));
        std::cout << "A new peer has connected." << std::endl;
      }

      /* Check all the existing connections */
      for (auto it = connected.begin(); it != connected.end(); it++) {
        /* If I can recv from a peer */
        if (FD_ISSET(it->fd, &readfds)) {
          char sockbuf[1024];
          int ayy = recv(it->fd, sockbuf, sizeof(sockbuf), MSG_DONTWAIT);
          if (ayy == 0) {
            /* Orderly shutdown */
            close(it->fd);
            it = connected.erase(it);
            if (it != connected.begin()) it--;
            std::cout << "Peer disconnected!" << std::endl;
            continue;
          }
          std::cout << "Received " << ayy << " bytes from a peer." << std::endl;
        }

        /* If I can send to a peer */
        if (FD_ISSET(it->fd, &writefds)) {
          /* write something */
          if (it->I_initiated && !it->have_sent_handshake) {
            std::cout << "Ooops I have to send a handshake!" << std::endl;
          }
        }
      }
    }
  }
}



size_t SaveResponseToVector(char *ptr, size_t size, size_t nmemb, void *userdata) {
  std::vector<char> *buf = reinterpret_cast<std::vector<char> *>(userdata);
  size_t old_size = buf->size();
  buf->resize(old_size + size*nmemb);
  for (int i = 0; i < size*nmemb; i++) {
    buf->data()[old_size + i] = ptr[i];
  }
  return size*nmemb;
}

Torrent BeginTorrentDownload(std::string path_to_torrent_file) {
  /* First, initialize curl */
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
  url << "?info_hash=" << escaped_info_hash << "&peer_id="
      << escaped_peer_id
      << "&port=6881&event=started&uploaded=0&downloaded=0&left=" << torrent_size;

  std::cout << url.str() << std::endl;

  /* Make the request and save the response */
  std::vector<char> tracker_response;
  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SaveResponseToVector);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &tracker_response);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
  if (curl_easy_perform(curl) != 0)
    throw std::logic_error("curl_easy_perform() failed");

  /* Decode the response */
  BencodeObj *resp_root = BencodeDecode(tracker_response);

  curl_free(escaped_info_hash);
  curl_free(escaped_peer_id);
  curl_easy_cleanup(curl);

  return Torrent(root, resp_root);
}

int main(int argc, char* argv[]) {
  if (argc != 2) return 0;
  std::vector<Torrent> torrents;
  std::thread networker(NetworkerEntry, &torrents);
  Torrent the_torrent = BeginTorrentDownload(argv[1]);
  torrents.push_back(the_torrent);
  networker.join();
  return 0;
}
