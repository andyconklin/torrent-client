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
  virtual void print(std::ostream &stream) const;
  virtual BencodeObj *get(int index);
  virtual BencodeObj *get(std::string key);
};

class BencodeInt : public BencodeObj {
public:
  int value;
  BencodeInt(int value, std::pair<int, int> bounds);
  virtual void print(std::ostream &stream) const;
};

class BencodeList : public BencodeObj {
private:
  std::vector<BencodeObj *> list;
public:
  BencodeList(std::vector<BencodeObj *> list, std::pair<int, int> bounds);
  virtual void print(std::ostream &stream) const;
  virtual ~BencodeList();
  virtual BencodeObj *get(int index);
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
  std::string get_value();
  virtual void print(std::ostream &stream) const;
};

BencodeObj *BencodeDecode(std::vector<char>::const_iterator const it_begin,
    std::vector<char>::const_iterator &it,
    std::vector<char>::const_iterator const it_end);

#endif
