#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <fstream>
#include <utility>

class BencodeObj {
public:
	virtual ~BencodeObj() {}
	virtual void print(std::ostream &stream) const { }
};

class BencodeInt : public BencodeObj {
private:
  int value;
public:
  BencodeInt(int value) : value(value) { }
	virtual void print(std::ostream &stream) const {
		stream << "i" << value << "e";
	}
};

class BencodeList : public BencodeObj {
private:
  std::vector<BencodeObj *> list;
public:
	BencodeList(std::vector<BencodeObj *> list) : list(list) {}
	virtual void print(std::ostream &stream) const {
		stream << "l";
		for (auto it = list.cbegin(); it != list.cend(); it++) {
			(*it)->print(stream);
		}
		stream << "e";
	}
	virtual ~BencodeList() {
		for (auto it = list.cbegin(); it != list.cend(); it++) {
			delete *it;
		}
	}
};

class BencodeDict : public BencodeObj {
private:
  std::map<std::string, BencodeObj *> dict;
public:
	BencodeDict(std::map<std::string, BencodeObj *> dict) : dict(dict) {}
	virtual void print(std::ostream &stream) const {
		stream << "d";
		for (auto it = dict.cbegin(); it != dict.cend(); it++) {
			stream << it->first.size() << ":" << it->first;
			it->second->print(stream);
		}
		stream << "e";
	}
	virtual ~BencodeDict() {
		for (auto it = dict.cbegin(); it != dict.cend(); it++) {
			delete it->second;
		}
	}
};

class BencodeString : public BencodeObj {
private:
	std::string value;
public:
	BencodeString(std::string value) : value(value) {}
	std::string get_value() { return value; }
	virtual void print(std::ostream &stream) const {
		stream << value.size() << ":" << value;
	}
};

BencodeObj *BencodeDecode(std::vector<char>::const_iterator &it,
    std::vector<char>::const_iterator &it_end) {
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
		return new BencodeInt(std::stoi(int_text));
  } else if (*it == 'l') {
		std::vector<BencodeObj *> list;
    while (*(++it) != 'e') {
      list.push_back(BencodeDecode(it, it_end));
    }
    return new BencodeList(list);
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
		return new BencodeString(s);
  } else if (*it == 'd') {
		std::map<std::string, BencodeObj *> d;
    while (*(++it) != 'e') {
      std::string k = (dynamic_cast<BencodeString *>(BencodeDecode(it, it_end)))->get_value();
      d.insert(make_pair(k, BencodeDecode(++it, it_end)));
    }
		return new BencodeDict(d);
  } else {
    throw std::logic_error("Unrecognized bencode type");
  }
  return NULL;
}

int main() {
  std::ifstream file("hello.txt", std::ios::binary | std::ios::ate);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (file.read(buffer.data(), size))
  {
    auto start = buffer.cbegin();
    auto end = buffer.cend();
    BencodeDecode(start, end)->print(std::cout);
  }
  return 0;
}
