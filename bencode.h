#ifndef BENCODE_H
#define BENCODE_H

#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <fstream>
#include <utility>

class BencodeObj {
public:
	std::pair<int, int> bounds;
	BencodeObj(std::pair<int, int> bounds);
	virtual ~BencodeObj();
	virtual BencodeObj *get(int index);
	virtual BencodeObj *get(std::string key);
	virtual void print(std::ostream &stream) const;
	virtual int get_int() const;
	virtual std::string get_string() const;
	virtual std::vector<BencodeObj *> &get_list();
};

class BencodeInt : public BencodeObj {
private:
	int value;
public:
	BencodeInt(int value, std::pair<int, int> bounds);
	virtual void print(std::ostream &stream) const;
	virtual int get_int() const;
};

class BencodeList : public BencodeObj {
private:
	std::vector<BencodeObj *> list;
public:
	BencodeList(std::vector<BencodeObj *> list, std::pair<int, int> bounds);
	virtual void print(std::ostream &stream) const;
	virtual ~BencodeList();
	virtual BencodeObj *get(int index);
	virtual std::vector<BencodeObj *> &get_list();
};

class BencodeDict : public BencodeObj {
private:
	std::map<std::string, BencodeObj *> dict;
public:
	BencodeDict(std::map<std::string, BencodeObj *> dict, std::pair<int, int> bounds);
	virtual void print(std::ostream &stream) const;
	virtual ~BencodeDict();
	virtual BencodeObj *get(std::string key);
};

class BencodeString : public BencodeObj {
private:
	std::string value;
public:
	BencodeString(std::string value, std::pair<int, int> bounds);
	virtual void print(std::ostream &stream) const;
	virtual std::string get_string() const;
};

BencodeObj *BencodeDecode(std::vector<char> const &buffer);

#endif