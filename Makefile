main: main.cpp bencode.cpp bencode.h
  g++ main.cpp bencode.cpp -o main -std=c++11 -lcrypto
