#include "bencode.h"

BencodeObj::BencodeObj(std::pair<int, int> bounds) : bounds(bounds) {}
BencodeObj::~BencodeObj() {}
void BencodeObj::print(std::ostream &stream) const {}
BencodeObj *BencodeObj::get(int index) {
  throw std::logic_error("Called get(int) on something other than BencodeList");
}
BencodeObj *BencodeObj::get(std::string key) {
  throw std::logic_error("Called get(str) on something other than BencodeDict");
}
int BencodeObj::get_int() const {
  throw std::logic_error("Called get_int on something other than BencodeInt");
}
std::string BencodeObj::get_string() const {
  throw std::logic_error("Called get_string on something other than BencodeString");
}
std::vector<BencodeObj *> & BencodeObj::get_list() {
  throw std::logic_error("Called get_list on something other than BencodeList");
}

BencodeInt::BencodeInt(int value, std::pair<int, int> bounds) : BencodeObj(bounds), value(value) { }
void BencodeInt::print(std::ostream &stream) const {
  stream << "i" << value << "e";
}
int BencodeInt::get_int() const { return value; }

BencodeList::BencodeList(std::vector<BencodeObj *> list, std::pair<int, int> bounds) : BencodeObj(bounds), list(list) {}
void BencodeList::print(std::ostream &stream) const {
  stream << "l";
  for (auto it = list.cbegin(); it != list.cend(); it++) {
    (*it)->print(stream);
  }
  stream << "e";
}
BencodeList::~BencodeList() {
  for (auto it = list.begin(); it != list.end(); it++) {
    delete *it;
  }
}
BencodeObj *BencodeList::get(int index) {
  return list.at(index);
}
std::vector<BencodeObj *> & BencodeList::get_list() {
  return list;
}

BencodeDict::BencodeDict(std::map<std::string, BencodeObj *> dict, std::pair<int, int> bounds) : BencodeObj(bounds), dict(dict) {}
void BencodeDict::print(std::ostream &stream) const {
  stream << "d";
  for (auto it = dict.cbegin(); it != dict.cend(); it++) {
    stream << it->first.size() << ":" << it->first;
    it->second->print(stream);
  }
  stream << "e";
}
BencodeDict::~BencodeDict() {
  for (auto it = dict.begin(); it != dict.end(); it++) {
    delete it->second;
  }
}
BencodeObj *BencodeDict::get(std::string key) {
  return dict.at(key);
}

BencodeString::BencodeString(std::string value, std::pair<int, int> bounds) : BencodeObj(bounds), value(value) {}
std::string BencodeString::get_string() const { return value; }
void BencodeString::print(std::ostream &stream) const {
  stream << value.size() << ":" << value;
}

/* This should not be called outside of this file...
   Call BencodeObj *BencodeDecode(std::vector<char> const &buffer) instead. */
namespace {
BencodeObj *BencodeDecode(std::vector<char>::const_iterator const it_begin,
    std::vector<char>::const_iterator &it,
    std::vector<char>::const_iterator const it_end) {
  int first_char = std::distance(it_begin, it);
  if (*it == 'i') {
    std::vector<char>::const_iterator start;
    for (start = ++it; it < it_end && *it != 'e'; it++) {
      if ((*it == '-' && it != start) || ((*it < '0' || *it > '9') && *it != '-'))
        throw std::logic_error("Integer parsing: Invalid character");
    }
    if (it == it_end)
      throw std::out_of_range("Integer parsing: Premature end");
    std::string int_text(start, it);
    if ((int_text[0] == '0' && int_text.size() > 1) ||
        (int_text[0] == '-' && (int_text.size() == 1 || int_text[1] == '0')))
      throw std::logic_error("Integer parsing: Leading zero");
    return new BencodeInt(std::stoi(int_text), std::make_pair(first_char, std::distance(it_begin, it)));
  } else if (*it == 'l') {
    std::vector<BencodeObj *> list;
    while (*(++it) != 'e') {
      list.push_back(BencodeDecode(it_begin, it, it_end));
    }
    return new BencodeList(list, std::make_pair(first_char, std::distance(it_begin, it)));
  } else if (*it >= '0' && *it <= '9') {
    std::vector<char>::const_iterator start;
    for (start = it; it < it_end && *it >= '0' && *it <= '9'; it++);
    if (it == it_end)
      throw std::out_of_range("String parsing: Premature end");
    else if (*it != ':')
      throw std::logic_error("String parsing: Unexpected character");
    int len = std::stoi(std::string(start, it));
    if (it + len >= it_end)
      throw std::logic_error("String parsing: String too long for file size");
    std::string s(it + 1, it + 1 + len);
    it += len;
    return new BencodeString(s, std::make_pair(first_char, std::distance(it_begin, it)));
  } else if (*it == 'd') {
    std::map<std::string, BencodeObj *> d;
    while (*(++it) != 'e') {
      std::string k = BencodeDecode(it_begin, it, it_end)->get_string();
      d.insert(std::make_pair(k, BencodeDecode(it_begin, ++it, it_end)));
    }
    return new BencodeDict(d, std::make_pair(first_char, std::distance(it_begin, it)));
  } else {
    throw std::logic_error("Unrecognized bencode type");
  }
  return NULL;
}
} /* namespace */

BencodeObj *BencodeDecode(std::vector<char> const &buffer) {
  auto curr = buffer.cbegin();
  return BencodeDecode(buffer.cbegin(), curr, buffer.cend());
}
