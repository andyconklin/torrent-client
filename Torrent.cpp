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
}

Torrent::Piece::Piece(unsigned char const *metahash, int piecelen) :
    I_have(false), buffer(std::vector<char>(piecelen)),
    have_chunk(std::vector<bool>((piecelen+0x3fff)/0x4000)) {
  memcpy(hash, metahash, 20);
}

struct sockaddr_in Torrent::yield_peer() {
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

  std::string peers = response->get("peers")->get_string();
  for (int i = 0; i < 4; i++) {
    reinterpret_cast<char*>(&(addr.sin_addr))[i] = peers.at(peer_index*6 + i);
  }
  for (int i = 4; i < 6; i++) {
    reinterpret_cast<char*>(&(addr.sin_port))[i-4] = peers.at(peer_index*6 + i);
  }

  peer_index++;
  return addr;
}

std::vector<char> Torrent::handshake() {
  std::vector<char> shake(68);
  shake[0] = 19;
  for (int i = 0; i < 19; i++) shake[1+i] = "BitTorrent protocol"[i];
  for (int i = 0; i < 8; i++) shake[20+i] = 0;
  for (int i = 0; i < 20; i++) shake[28+i] = info_hash[i];
  for (int i = 0; i < 20; i++) shake[48+i] = peer_id[i];
  return shake;
}

std::vector<char> Torrent::bitfield() {
  int len = (pieces.size() + 7) / 8;
  std::vector<char> ret(5+len, 0);
  *reinterpret_cast<unsigned int *>(&ret[0]) = htonl(len+1);
  ret[4] = 5;
  for (unsigned int i = 0; i < pieces.size(); i++) {
    if (pieces[i].I_have) ret[6+i/8] |= (1 << (7 - i));
  }
  return ret;
}

std::vector<char> Torrent::yield_request(Peer *peer) {
  std::vector<char> ret;
  if (peer->allowed_requests < 5) return ret;
  unsigned int p = 0;
  for (unsigned int i = 0; i < peer->bitfield.size(); i++) {
    if (peer->bitfield[i] && !pieces[i].I_have) {
      p = i;
      break;
    }
  }
  for (unsigned int i = 0; i < pieces.at(p).buffer.size(); i += 0x4000) {
    if (pieces.at(p).have_chunk[i/0x4000]) continue;
    std::vector<char> request_out(17, 0);
    request_out[3] = 13;
    request_out[4] = 6;
    *reinterpret_cast<unsigned int *>(&request_out[5]) = htonl(p);
    *reinterpret_cast<unsigned int *>(&request_out[9]) = htonl(i);
    if (i + 0x4000 >= pieces.at(p).buffer.size())
      *reinterpret_cast<unsigned int *>(&request_out[13]) =
        htonl(pieces.at(p).buffer.size() - i);
    else
      *reinterpret_cast<unsigned int *>(&request_out[13]) = htonl(0x4000);
    ret.insert(ret.end(), request_out.begin(), request_out.end());
    peer->allowed_requests--;
    if (peer->allowed_requests <= 0) break;
  }
  return ret;
}


void Torrent::place_piece(unsigned int index, unsigned int begin,
    char const *buf, unsigned int length) {
  Piece &p = pieces.at(index);
  p.have_chunk.at(begin/0x4000) = true;
  /* TODO this is clearly not safe */
  memcpy(&p.buffer[begin], buf, length);
  bool try_to_hash = true;
  for (unsigned int i = 0; i < p.have_chunk.size(); i++) {
    if (!p.have_chunk.at(i)) {
      try_to_hash = false;
      break;
    }
  }
  if (try_to_hash) {
    unsigned char found_hash[20];
    SHA1(reinterpret_cast<const unsigned char *>(p.buffer.data()),
        p.buffer.size(), found_hash);
    if (true) { //memcmp(p.hash, found_hash, 20) == 0) {
      p.I_have = true;
      std::cout << "Acquired piece " << index << "!" << std::endl;
      for (unsigned int i = 0; i < pieces.size(); i++) {
        if (!pieces.at(i).I_have) {
          try_to_hash = false;
          break;
        }
      }
      if (try_to_hash) {
        std::cout << "I think I have all the pieces now." << std::endl;
        for (unsigned int i = 0; i < pieces.size(); i++) {
          for (unsigned int j = 0; j < pieces.at(i).buffer.size(); j++) {
            std::cout << *(unsigned char *)(&pieces.at(i).buffer.at(j));
          }
        }
      }
    } else {
      for (unsigned int i = 0; i < p.have_chunk.size(); i++) {
        p.I_have = false;
        p.have_chunk.at(i) = false;
        std::cout << "Piece " << index << " hashed wrong. Clearing." << std::endl;
      }
    }
  }
}

Torrent::Torrent(std::string path_to_torrent_file) : peer_index(0){
  /* First, initialize curl */
  CURL *curl = curl_easy_init();
  if (!curl) throw std::logic_error("curl_easy_init() failed");

  /* Read and decode the .torrent file */
  std::vector<char> the_file = ReadFile(path_to_torrent_file);
  metainfo = BencodeDecode(the_file);

  /* Get the info dict */
  BencodeObj *info = metainfo->get("info");

  /* Populate pieces */
  int num_pieces = info->get("pieces")->get_string().size() / 20;
  for (int i = 0; i < num_pieces; i++) {
    int piece_size = info->get("piece length")->get_int();
    if (i == num_pieces-1)
      if ((torrent_size % piece_size) != 0)
        piece_size = (torrent_size % piece_size);
    pieces.push_back(Piece(reinterpret_cast<unsigned char const *>(
        &(info->get("pieces")->get_string()[i*20])), piece_size));
  }

  /* Calculate info_hash */
  SHA1(reinterpret_cast<const unsigned char *>(&the_file[info->bounds.first]),
      1 + info->bounds.second - info->bounds.first, info_hash);

  /* Randomly generate peer_id */
  RAND_bytes(peer_id, sizeof(peer_id));

  /* Calculate torrent size */
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
  } catch (std::out_of_range &e) { }

  curl_free(escaped_info_hash);
  curl_free(escaped_peer_id);
  curl_easy_cleanup(curl);
}
