/**
 * @example CustomValueType.cc
 *
 * The program demonstrates how to define functors for comparisons and hashing
 * needed to support custom value types.
 *
 * Copyright (c) 2017 Olle Liljenzin
 **/

#include <cassert>
#include <functional>
#include <string>

#include "map.h"
#include "set.h"

namespace {

class MyKey {
 public:
  MyKey(const std::string& s, float f) : s_(s), f_(f) {}

  const std::string& getString() const { return s_; }
  float getFloat() const { return f_; }

 private:
  std::string s_;
  float f_;
};

class MyMappedValue {
 public:
  MyMappedValue(int x, int y) : x_(x), y_(y) {}

  int getX() const { return x_; }
  int getY() const { return y_; }

 private:
  int x_;
  int y_;
};

struct MyKeyCompare {
  bool operator()(const MyKey& lhs, const MyKey& rhs) const {
    if (lhs.getString() < rhs.getString())
      return true;
    if (lhs.getString() == rhs.getString())
      return lhs.getFloat() < rhs.getFloat();
    return false;
  }
};

struct MyKeyEqual {
  bool operator()(const MyKey& lhs, const MyKey& rhs) const {
    return lhs.getString() == rhs.getString() &&
           lhs.getFloat() == rhs.getFloat();
  }
};

struct MyKeyHash {
  size_t operator()(const MyKey& key) const {
    std::hash<std::string> h1;
    std::hash<float> h2;

    return h1(key.getString()) * 32 + h2(key.getFloat());
  }
};

struct MyMappedEqual {
  bool operator()(const MyMappedValue& lhs, const MyMappedValue& rhs) const {
    return lhs.getX() == rhs.getX() && lhs.getY() == rhs.getY();
  }
};

struct MyMappedHash {
  size_t operator()(const MyMappedValue& value) const {
    return value.getX() * 32 + value.getY();
  }
};

typedef confluent::set<MyKey, MyKeyCompare, MyKeyHash, MyKeyEqual> MySet;

typedef confluent::map<MyKey,
                       MyMappedValue,
                       MyKeyCompare,
                       MyKeyHash,
                       MyKeyEqual,
                       MyMappedHash,
                       MyMappedEqual>
    MyMap;

}  // namespace

int main() {
  MyKey k1("k1", 1.0);
  MyKey k2("k2", 2.0);
  MyMappedValue v1(1, 2);
  MyMappedValue v2(2, 2);

  MySet s = {k1, k2};
  MyMap m = {{k1, v1}, {k2, v2}};

  assert(s == m.key_set());
}
