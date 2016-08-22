#ifndef TORRENT_H
#define TORRENT_H

#include <vector>
#include <string>
#include <netinet/in.h>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <cstring>

#include "bencode.h"
#include "Peer.h"

struct Torrent {
  struct Piece {
    bool I_have;
    unsigned char hash[20];
    std::vector<char> buffer;
    Piece(unsigned char const *metahash, int piecelen);
  };
  BencodeObj *metainfo;
  BencodeObj *response;
  unsigned char info_hash[20];
  unsigned char peer_id[20];
  int torrent_size;
  int peer_index;
  std::vector<struct Peer> peers;
  std::vector<struct Piece> pieces;

  Torrent(std::string path_to_torrent_file);
  struct sockaddr_in yield_peer();
  std::vector<char> handshake();
  std::vector<char> bitfield();
};

#endif
