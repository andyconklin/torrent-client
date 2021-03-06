#ifndef TORRENT_H
#define TORRENT_H

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <WinSock2.h>
#include <winhttp.h>
#include <iomanip>
#include <curl/curl.h>

#include "bencode.h"
#include "Peer.h"

struct Torrent {
	struct Piece {
		bool I_have;
		unsigned char hash[20];
		std::vector<char> buffer;
		std::vector<std::time_t> sliver_status;
		int piece_size;
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
	void place_piece(unsigned int index, unsigned int begin, char const *buf, unsigned int length);
	std::vector<char> gimme_five_slivers();
};

#endif