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
  virtual ~BencodeObj();
  virtual void print(std::ostream &stream) const;
  virtual BencodeObj *get(int index);
};

class BencodeInt : public BencodeObj {
private:
  int value;
public:
  BencodeInt(int value);
  virtual void print(std::ostream &stream) const;
};

class BencodeList : public BencodeObj {
private:
  std::vector<BencodeObj *> list;
public:
  BencodeList(std::vector<BencodeObj *> list);
  virtual void print(std::ostream &stream) const;
  virtual ~BencodeList();
  virtual BencodeObj *get(int index);
};

class BencodeDict : public BencodeObj {
private:
  std::map<std::string, BencodeObj *> dict;
public:
  BencodeDict(std::map<std::string, BencodeObj *> dict);
  virtual void print(std::ostream &stream) const;
  virtual ~BencodeDict();
};

class BencodeString : public BencodeObj {
private:
  std::string value;
public:
  BencodeString(std::string value);
  std::string get_value();
  virtual void print(std::ostream &stream) const;
};

BencodeObj *BencodeDecode(std::vector<char>::const_iterator &it,
    std::vector<char>::const_iterator &it_end);

#endif
