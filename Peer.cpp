#include "Peer.h"

Peer::Peer(Torrent *torrent, int fd, bool initiated) :
	torrent(torrent), fd(fd),
	received_from(std::time(NULL)), sent_to(std::time(NULL)),
	am_interested(false), am_choked(true), is_interested(false),
	is_choked(true), last_request(0), allow_requests(5) {
	if (initiated) state = NOT_EVEN_CONNECTED;
	else state = I_AM_EXPECTING_THE_FIRST_HANDSHAKE;
	for (int i = 0; i < 20; i++) peer_id[i] = 0;
}

bool Peer::process_one_message() {
	if (inbuf.size() == 0) return false;

	if (state != I_AM_EXPECTING_THE_FIRST_HANDSHAKE &&
		state != I_AM_EXPECTING_THE_SECOND_HANDSHAKE &&
		static_cast<unsigned int>(4 + TOINT(inbuf[0])) > inbuf.size())
		return false;

	int num_read = 0;

	if (state == I_AM_EXPECTING_THE_FIRST_HANDSHAKE ||
		state == I_AM_EXPECTING_THE_SECOND_HANDSHAKE) {
		if (inbuf.size() < 68) return false;
		if (torrent == NULL) throw NeedTorrent(&inbuf[28]);
		if (inbuf[0] != 19) throw std::logic_error("Malformed handshake.");
		for (int i = 0; i < 19; i++) {
			if (inbuf[1 + i] != "BitTorrent protocol"[i])
				throw std::logic_error("Malformed handshake.");
		}
		for (int i = 0; i < 20; i++) {
			if (inbuf[28 + i] != torrent->info_hash[i])
				throw std::logic_error("info_hash mismatch.");
		}
		/* Otherwise, collect the peer_id */
		for (int i = 0; i < 20; i++) {
			peer_id[i] = inbuf[48 + i];
		}
		/* Initialize the bitfield */
		bitfield.resize(((torrent->pieces.size() + 7) / 8) * 8);
		for (unsigned int i = 0; i < bitfield.size(); i++)
			bitfield[i] = false;
		num_read = 68;
		if (state == I_AM_EXPECTING_THE_FIRST_HANDSHAKE)
			state = I_NEED_TO_SEND_THE_SECOND_HANDSHAKE;
		else
			state = I_SHOULD_SEND_BITFIELD;
	}
	else if (TOINT(inbuf[0]) == 0) {
		num_read = 4;
	}
	else {
		if (TOINT(inbuf[0]) == 1) {
			if (inbuf[4] == 0) is_choked = true;
			else if (inbuf[4] == 1) is_choked = false;
			else if (inbuf[4] == 2) is_interested = true;
			else if (inbuf[4] == 3) is_interested = false;
			else throw std::logic_error("Unrecognized message. ABC");
			num_read = 5;
		}
		else {
			if (inbuf[4] == 4) { /* HAVE */
				if (TOINT(inbuf[0]) == 5) {
					unsigned int index = TOINT(inbuf[5]);
					bitfield[index] = true;
					num_read = 9;
				}
				else throw std::logic_error("Malformed HAVE message.");
			}
			else if (inbuf[4] == 5) { /* BITFIELD */
				unsigned int b = bitfield.size() / 8;
				if (TOINT(inbuf[0]) == 1 + b) {
					for (unsigned int i = 0; i < torrent->pieces.size(); i++) {
						unsigned int byte = i / 8;
						unsigned int bit = 7 - (i % 8);
						bitfield[i] = ((inbuf[5 + byte] & (1 << bit)) != 0);
					}
					num_read = 5 + b;
				}
				else throw std::logic_error("Malformed BITFIELD message.");
			}
			else if (inbuf[4] == 6) { /* REQUEST */
				throw std::logic_error("REQUEST is not implemented.");
			}
			else if (inbuf[4] == 7) { /* PIECE */
				std::cout << "I got a piece message." << std::endl;
				torrent->place_piece(TOINT(inbuf[5]), TOINT(inbuf[9]),
					reinterpret_cast<char const *>(&inbuf[13]), TOINT(inbuf[0]) - 9);
				num_read = 4 + TOINT(inbuf[0]);
			}
			else if (inbuf[4] == 8) { /* CANCEL */
				throw std::logic_error("CANCEL is not implemented.");
			}
			else {
				throw std::logic_error("Unrecognized message. DEF");
			}
		}
	}
	inbuf.erase(inbuf.begin(), inbuf.begin() + num_read);
	return true;
}

void Peer::process_response(char *buf, int buflen) {
	if (buflen > 0 && buf != NULL) {
		received_from = std::time(NULL);
		int old_size = inbuf.size();
		inbuf.resize(old_size + buflen);
		memcpy(&inbuf[old_size], buf, buflen);
	}
	while (process_one_message());
}

/* The client is interested whenever the peer has a piece that 
   the client lacks. */
bool Peer::update_interest() {
	bool old_interest = am_interested;
	for (int i = 0; i < torrent->pieces.size(); i++) {
		if (!torrent->pieces[i].I_have && bitfield[i]) {
			am_interested = true;
			break;
		}
	}
	return old_interest != am_interested;
}

std::vector<unsigned char> Peer::to_send() {
	if (state == I_NEED_TO_SEND_THE_FIRST_HANDSHAKE ||
		state == I_NEED_TO_SEND_THE_SECOND_HANDSHAKE) {
		std::vector<char> shake = torrent->handshake();
		outbuf.insert(outbuf.end(), shake.begin(), shake.end());
		if (state == I_NEED_TO_SEND_THE_FIRST_HANDSHAKE) {
			state = I_AM_EXPECTING_THE_SECOND_HANDSHAKE;
		}
		else
			state = I_SHOULD_SEND_BITFIELD;
	}
	else if (state == I_SHOULD_SEND_BITFIELD) {
		std::vector<char> bits = torrent->bitfield();
		outbuf.insert(outbuf.end(), bits.begin(), bits.end());
		state = INTRO_IS_FINISHED;
	}
	else if (state == INTRO_IS_FINISHED) {
		/* Did interest state change? */
		if (update_interest()) {
			std::vector<char> interest_out(5, 0);
			interest_out[3] = 1;
			interest_out[4] = (am_interested) ? 2 : 3;
			outbuf.insert(outbuf.end(), interest_out.begin(), interest_out.end());
		}
		/* Should I request something? */
		if (!is_choked && am_interested && outbuf.size() == 0) {
			/* if it's been fifteen seconds we're gonna allow more requests */
			if (difftime(std::time(NULL), last_request) >= 15)
				allow_requests = 5;
			/* otherwise if five requests have been fulfilled then we're gonna allow more */
			if (allow_requests >= 5) {
				/* pick the first five slivers that haven't been requested by anyone else
				in fifteen seconds that we don't already have */
				std::vector<char> buf = torrent->gimme_five_slivers();
				/* request em */
				outbuf.insert(outbuf.end(), buf.begin(), buf.end());
				allow_requests = 5 - (buf.size() / 17);
				last_request = std::time(NULL);
			}
		}
	}
	return outbuf;
}

void Peer::just_sent(int bytes) {
	sent_to = std::time(NULL);
	outbuf.erase(outbuf.begin(), outbuf.begin() + bytes);
}