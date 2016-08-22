#ifndef PEER_H
#define PEER_H

#include <vector>
#include <ctime>

#include "Torrent.h"

#define TOINT(x) (ntohl(*reinterpret_cast<unsigned int *>(&x)))

struct NeedTorrent {
  unsigned char info_hash[20];
  NeedTorrent(unsigned char *x) {
    for (int i = 0; i < 20; i++) info_hash[i] = x[i];
  }
};

struct Peer {
  enum State {
    I_AM_EXPECTING_THE_FIRST_HANDSHAKE,
    I_NEED_TO_SEND_THE_FIRST_HANDSHAKE,
    I_AM_EXPECTING_THE_SECOND_HANDSHAKE,
    I_NEED_TO_SEND_THE_SECOND_HANDSHAKE,
    I_SHOULD_SEND_BITFIELD,
    INTRO_IS_FINISHED
  };

  struct Torrent *parent_torrent;
  int fd;
  std::time_t received_from;
  std::time_t sent_to;
  unsigned char peer_id[20];
  bool am_interested;
  bool am_choked;
  bool is_interested;
  bool is_choked;
  std::vector<unsigned char> inbuf;
  std::vector<unsigned char> outbuf;
  std::vector<bool> bitfield;
  State state;

  Peer(struct Torrent *parent_torrent, int fd, bool initiated);
  bool process_one_message();
  void process_response(char *buf, int buflen);
  std::vector<unsigned char> to_send();
  void just_sent(int bytes);
  bool update_interest();
};
#endif
