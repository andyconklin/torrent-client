#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <fstream>

class BencodeObj {

};

class BencodeInt : BencodeObj {
private:
	int value;
public:
	BencodeInt(int v) : value(v) { }
	int get_value() const { return value; }
	void set_value(int v) { value = v; }
};

class BencodeList : BencodeObj {
private:
	std::vector<BencodeObj *> list;
public:

};

class BencodeDict : BencodeObj {
private:
	std::map<std::string, BencodeObj *> dict;
public:

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
		std::cout << "Hey it's correctly formatted!! " << int_text << std::endl;
	} else if (*it == 'l') {
		std::cout << "Start list" << std::endl;
		while (*(++it) != 'e') {
			BencodeDecode(it, it_end);
		}
		std::cout << "End list" << std::endl;
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
		std::cout << "Dope string: " << std::string(it + 1, it + 1 + len) << std::endl;
		it += len;
	} else if (*it == 'd') {
		std::cout << "Start dict" << std::endl;
		while (*(++it) != 'e') {
			std::cout << "Key: ";
			BencodeDecode(it, it_end);
			std::cout << "Value: ";
			BencodeDecode(++it, it_end);
		}
		std::cout << "End dict" << std::endl;
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
			BencodeDecode(start, end);
	}
	return 0;
}
