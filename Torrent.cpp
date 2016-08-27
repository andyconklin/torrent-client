#include "Torrent.h"

namespace {
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

	size_t SaveResponseToVector(char *ptr, size_t size, size_t nmemb, void *userdata) {
		std::vector<char> *buf = reinterpret_cast<std::vector<char> *>(userdata);
		size_t old_size = buf->size();
		buf->resize(old_size + size*nmemb);
		for (unsigned int i = 0; i < size*nmemb; i++) {
			buf->data()[old_size + i] = ptr[i];
		}
		return size*nmemb;
	}

	void SHA1(const unsigned char *inbuf, unsigned int len, unsigned char *outbuf) {
		HCRYPTPROV hprov;
		HCRYPTHASH hhash;
		DWORD twenty = 20;
		CryptAcquireContext(&hprov, NULL, NULL, PROV_RSA_FULL, 0);
		CryptCreateHash(hprov, CALG_SHA1, 0, 0, &hhash);
		CryptHashData(hhash, inbuf, len, 0);
		if (!CryptGetHashParam(hhash, HP_HASHVAL, outbuf, &twenty, 0))
			std::cout << "Failed to hash." << std::endl;
		CryptReleaseContext(hprov, 0);
	}

	void RAND_bytes(unsigned char *outbuf, unsigned int len) {
		HCRYPTPROV hprov;
		DWORD twenty = 20;
		CryptAcquireContext(&hprov, NULL, NULL, PROV_RSA_FULL, 0);
		if (!CryptGenRandom(hprov, len, outbuf))
			std::cout << "Failed to generate random bytes." << std::endl;
		CryptReleaseContext(hprov, 0);
	}
}

Torrent::Piece::Piece(unsigned char const *metahash, int piecelen) :
	I_have(false), buffer(std::vector<char>(piecelen)),
	sliver_status((piecelen + 0x3fff) / 0x4000, 0), piece_size(piecelen) {
	memcpy(hash, metahash, 20);
}

struct sockaddr_in Torrent::yield_peer() {
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

	std::string peers = response->get("peers")->get_string();
	for (int i = 0; i < 4; i++) {
		reinterpret_cast<char*>(&(addr.sin_addr))[i] = peers.at(peer_index * 6 + i);
	}
	for (int i = 4; i < 6; i++) {
		reinterpret_cast<char*>(&(addr.sin_port))[i - 4] = peers.at(peer_index * 6 + i);
	}

	peer_index++;
	return addr;
}

std::vector<char> Torrent::handshake() {
	std::vector<char> shake(68);
	shake[0] = 19;
	for (int i = 0; i < 19; i++) shake[1 + i] = "BitTorrent protocol"[i];
	for (int i = 0; i < 8; i++) shake[20 + i] = 0;
	for (int i = 0; i < 20; i++) shake[28 + i] = info_hash[i];
	for (int i = 0; i < 20; i++) shake[48 + i] = peer_id[i];
	return shake;
}

std::vector<char> Torrent::bitfield() {
	int len = (pieces.size() + 7) / 8;
	std::vector<char> ret(5 + len, 0);
	*reinterpret_cast<unsigned int *>(&ret[0]) = htonl(len + 1);
	ret[4] = 5;
	for (unsigned int i = 0; i < pieces.size(); i++) {
		if (pieces[i].I_have) ret[5 + i / 8] |= (1 << (7 - i));
	}
	return ret;
}

std::vector<char> Torrent::gimme_five_slivers() {
	std::vector<char> ret;
	for (int i = 0; i < 5; i++) {
		for (int j = pieces.size() - 1; j >= 0; j--) {
			if (pieces.at(j).I_have) continue;
			for (int k = 0; k < pieces.at(j).sliver_status.size(); k++) {
				std::time_t &t = pieces.at(j).sliver_status.at(k);
				if (t == 1) continue;
				if (difftime(std::time(NULL), t) < 15) continue;
				int end = 0x4000 * k + 0x4000;
				if (end >= pieces.at(j).buffer.size())
					end = pieces.at(j).buffer.size();

				pieces.at(j).sliver_status.at(k) = std::time(NULL);

				std::vector<char> request_out(17, 0);
				request_out[3] = 13;
				request_out[4] = 6;
				*reinterpret_cast<unsigned int *>(&request_out[5]) = htonl(j);
				*reinterpret_cast<unsigned int *>(&request_out[9]) = htonl(0x4000*k);
				*reinterpret_cast<unsigned int *>(&request_out[13]) = htonl(end - (0x4000 * k));

				ret.insert(ret.end(), request_out.begin(), request_out.end());
				goto the_next_sliver;
			}
		}
	the_next_sliver:
		continue;
	}
	return ret;
}

void Torrent::place_piece(unsigned int index, unsigned int begin,
	char const *buf, unsigned int length) {
	Piece &p = pieces.at(index);
	p.sliver_status.at(begin / 0x4000) = 1;
	/* TODO this is clearly not safe */
	memcpy(&p.buffer[begin], buf, length);
	bool try_to_hash = true;
	for (unsigned int i = 0; i < p.sliver_status.size(); i++) {
		if (p.sliver_status.at(i) != 1) {
			try_to_hash = false;
			break;
		}
	}
	if (try_to_hash) {
		unsigned char found_hash[20];
		SHA1(reinterpret_cast<const unsigned char *>(p.buffer.data()),
			p.buffer.size(), found_hash);
		if (memcmp(p.hash, found_hash, 20) == 0) {
			std::cout << "Acquired piece " << index << "." << std::endl;
			p.I_have = true;
			for (unsigned int i = 0; i < pieces.size(); i++) {
				if (!pieces.at(i).I_have) {
					try_to_hash = false;
					break;
				}
			}
			if (try_to_hash) {
				/* Download is complete. */
				std::cout << "Torrent has completed downloading." << std::endl;
				std::ofstream outfile("the_torrent.bin", std::ofstream::binary);
				for (unsigned int i = 0; i < pieces.size(); i++) {
					outfile.write(pieces.at(i).buffer.data(), pieces.at(i).buffer.size());
				}
				outfile.close();
			}
		}
		else {
			for (unsigned int i = 0; i < p.sliver_status.size(); i++) {
				p.I_have = false;
				p.sliver_status.at(i) = 0;
				std::cout << "Piece " << index << " hashed wrong. Clearing." << std::endl;
			}
		}
	}
}

Torrent::Torrent(std::string path_to_torrent_file) : peer_index(0) {

	/* First, initialize curl */
	CURL *curl = curl_easy_init();
	if (!curl) throw std::logic_error("curl_easy_init() failed");

	/* Read and decode the .torrent file */
	std::vector<char> the_file = ReadFile(path_to_torrent_file);
	metainfo = BencodeDecode(the_file);

	/* Get the info dict */
	BencodeObj *info = metainfo->get("info");

	/* Calculate info_hash */
	SHA1(reinterpret_cast<const unsigned char *>(&the_file[info->bounds.first]),
		1 + info->bounds.second - info->bounds.first, info_hash);

	/* Randomly generate peer_id */
	RAND_bytes(peer_id, sizeof(peer_id));

	/* Calculate torrent size */
	try {
		torrent_size = info->get("length")->get_int();
	}
	catch (std::exception &e) {
		torrent_size = 0;
		std::vector<BencodeObj *> list = info->get("files")->get_list();
		for (auto &obj : list) {
			torrent_size += obj->get("length")->get_int();
		}
	}

	/* Populate pieces */
	int num_pieces = info->get("pieces")->get_string().size() / 20;
	for (int i = 0; i < num_pieces; i++) {
		int piece_size = info->get("piece length")->get_int();
		if (i == num_pieces - 1)
			if ((torrent_size % piece_size) != 0)
				piece_size = (torrent_size % piece_size);
		pieces.push_back(Piece(reinterpret_cast<unsigned char const *>(
			&(info->get("pieces")->get_string()[i * 20])), piece_size));
	}

	/* URL encode the binary data */
	char *escaped_info_hash =
		curl_easy_escape(curl, reinterpret_cast<const char *>(info_hash), 20);
	char *escaped_peer_id =
		curl_easy_escape(curl, reinterpret_cast<const char *>(peer_id), 20);

	/* Generate URL of tracker request */
	std::ostringstream url;
	url << metainfo->get("announce")->get_string()
		<< "?info_hash=" << escaped_info_hash << "&peer_id="
		<< escaped_peer_id << "&port=6881&event=started&uploaded"
		"=0&downloaded=0&left=" << torrent_size;

	/* Make the request and save the response */
	std::vector<char> tracker_response;
	curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SaveResponseToVector);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &tracker_response);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	if (curl_easy_perform(curl) != 0)
		throw std::logic_error("curl_easy_perform() failed");

	/* Decode the response */
	response = BencodeDecode(tracker_response);

	try {
		if (response->get("failure reason"))
			throw std::logic_error(response->get("failure reason")->get_string());
	}
	catch (std::out_of_range &e) {}

	curl_free(escaped_info_hash);
	curl_free(escaped_peer_id);
	curl_easy_cleanup(curl);
}