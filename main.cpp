#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
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
#include "Torrent.h"
#include "Peer.h"

#define STOP_CONNECTING 30
#define STOP_ACCEPTING 55

Torrent *neediest(std::vector<Torrent> *torrents) {
  return &torrents->at(0);
}

void NetworkerEntry(std::vector<Torrent> *torrents) {
  /* http://stackoverflow.com/questions/2284428 */
  fd_set readfds, writefds;
  struct timeval timeout;
  int max_fd = 0;

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

    while (torrents->size() < 1);

    int connected = 0;

    for (auto x : *torrents) {
      x.update();
      connected += x.peers.size();
    }

    std::cout << connected << " connected peers." << std::endl;

    /* If we have under STOP_CONNECTING connections,
       then we should connect to a peer or several. */
    if (connected < STOP_CONNECTING) {
      int new_sock = socket(AF_INET, SOCK_STREAM, 0);
      int flags = fcntl(new_sock, F_GETFL);
      fcntl(new_sock, F_SETFL, flags | O_NONBLOCK);
      Torrent *needy = neediest(torrents);
      sockaddr_in addr = needy->yield_peer();
      std::cout << "Making a new connection..." << std::endl << std::flush;
      int res = connect(new_sock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
      if (res != 0 && res != EINPROGRESS) {
        std::cout << "What??" << std::strerror(errno) << std::endl;
      } else {
        Peer mynewpeer = Peer(needy, new_sock, true);
        if (res == 0) mynewpeer.state = Peer::I_NEED_TO_SEND_THE_FIRST_HANDSHAKE;
        needy->peers.push_back(mynewpeer);
      }
    }

    /* Check the listening socket for new connections */
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_SET(in_socket, &readfds);
    max_fd = (max_fd > in_socket) ? max_fd : in_socket + 1;

    /* Check the existing connections */
    for (Torrent &torrent : *torrents) {
      for (auto it = torrent.peers.begin(); it != torrent.peers.end(); it++) {
        if (it->state != Peer::NOT_EVEN_CONNECTED)
          FD_SET(it->fd, &readfds);
        if (it->to_send().size() > 0)
          FD_SET(it->fd, &writefds);
        max_fd = (max_fd > it->fd) ? max_fd : it->fd + 1;
      }
    }

    /* Spend up to 1 second looking for bytes to read */
    timeout.tv_sec = 1; timeout.tv_usec = 0;
    if (select(max_fd, &readfds, &writefds, NULL, &timeout) > 0) {
      /* Accept a new connection from the listening socket */
      if (FD_ISSET(in_socket, &readfds)) {
        std::cout << "Not ready to have people connect to me actually." << std::endl;
      }

      /* Check all the existing connections */
      for (Torrent &torrent : *torrents) {
        for (auto peer = torrent.peers.begin(); peer < torrent.peers.end(); peer++) {
          if (FD_ISSET(peer->fd, &readfds)) {
            char sockbuf[1024*1024];
            int resplen = recv(peer->fd, sockbuf, sizeof(sockbuf), MSG_DONTWAIT);
            if (resplen == 0) {
              /* Peer disconnected... orderly shutdown */
              std::cout << "Peer disconnected!" << std::endl;
              close(peer->fd);
              peer = torrent.peers.erase(peer);
              if (peer != torrent.peers.begin()) peer--;
              continue;
            } else if (resplen < 0) {
              std::cout << "(recv): An error occurred: " << std::strerror(errno) << std::endl;
            } else {
              try {
                peer->process_response(sockbuf, resplen);
              } catch (std::logic_error &e) {
                std::cout << "Caught error: " << e.what() << std::endl;
                std::cout << "Disconnecting..." << std::endl;
                close(peer->fd);
                peer = torrent.peers.erase(peer);
                if (peer != torrent.peers.begin()) peer--;
                continue;
              }
            }
          }

          if (FD_ISSET(peer->fd, &writefds)) {
            if (peer->state == Peer::NOT_EVEN_CONNECTED) {
              /* Oh, good. Our connection completed. */
              int res = 0;
              socklen_t reslen = sizeof(res);
              if (getsockopt(peer->fd, SOL_SOCKET, SO_ERROR, &res, &reslen) < 0 ||
                  res != 0) {
                std::cout << "Ah shoot it didn't work." << std::endl;
                close(peer->fd);
                peer = torrent.peers.erase(peer);
                if (peer != torrent.peers.begin()) peer--;
              } else {
                peer->state = Peer::I_NEED_TO_SEND_THE_FIRST_HANDSHAKE;
              }
              continue;
            }

            /* Should I send something to this peer? */
            std::vector<unsigned char> to_send = peer->to_send();
            if (to_send.size() <= 0) {
              std::cout << "I shouldn't even be here." << std::endl;
              continue;
            }

            /* Send whatever */
            int num_sent = send(peer->fd, to_send.data(), to_send.size(), MSG_DONTWAIT);
            if (num_sent <= 0) {
              std::cout << "Failed to send whatever. " << std::strerror(errno) << std::endl;
            } else {
              peer->just_sent(num_sent);
            }
          }
        }
      }
    }
  }
}

int main(int argc, char* argv[]) {
  if (argc != 2) return 0;
  std::vector<Torrent> torrents;
  std::thread networker(NetworkerEntry, &torrents);
  torrents.push_back(Torrent(argv[1]));
  networker.join();
  return 0;
}
