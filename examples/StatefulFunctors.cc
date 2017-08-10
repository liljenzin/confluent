/**
 * @example StatefulFunctors.cc
 *
 * The program demonstrates how to use stateful functors that cannot be default
 * constructed.
 *
 * Copyright (c) 2017 Olle Liljenzin
 **/

#include <iostream>
#include <string>

#include "map.h"
#include "set.h"

namespace {

struct StatefulKeyCompare {
  StatefulKeyCompare(bool ascending) : ascending(ascending) {}

  bool operator()(int lhs, int rhs) const {
    return ascending ? lhs < rhs : rhs < lhs;
  }

  const size_t ascending;
};

struct StatefulKeyHash {
  StatefulKeyHash(int seed) : seed(seed) {}
  size_t operator()(int key) const { return key ^ seed; }

  const int seed;
};

struct StatefulKeyEqual {
  StatefulKeyEqual(size_t count) : count(count) {}
  bool operator()(int lhs, int rhs) const {
    ++count;
    return lhs == rhs;
  }

  mutable size_t count;
};

struct StatefulMappedHash {
  StatefulMappedHash(size_t* counter) : counter(counter) {}

  size_t operator()(const std::string& value) const {
    ++*counter;
    return hasher(value);
  }

  std::hash<std::string> hasher;
  size_t* counter;
};

struct StatefulMappedEqual {
  StatefulMappedEqual(size_t* counter) : counter(counter) {}

  bool operator()(const std::string& lhs, const std::string& rhs) const {
    ++*counter;
    return lhs == rhs;
  }

  size_t* counter;
};

typedef confluent::
    set<int, StatefulKeyCompare, StatefulKeyHash, StatefulKeyEqual>
        MySet;
typedef confluent::
    set_provider<int, StatefulKeyCompare, StatefulKeyHash, StatefulKeyEqual>
        MySetProvider;

typedef confluent::map<int,
                       std::string,
                       StatefulKeyCompare,
                       StatefulKeyHash,
                       StatefulKeyEqual,
                       StatefulMappedHash,
                       StatefulMappedEqual>
    MyMap;

typedef confluent::map_provider<int,
                                std::string,
                                StatefulKeyCompare,
                                StatefulKeyHash,
                                StatefulKeyEqual,
                                StatefulMappedHash,
                                StatefulMappedEqual>
    MyMapProvider;

}  // namespace

int main() {
  // Sets using this provider will be sorted in ascending order.
  std::shared_ptr<MySetProvider> setProvider1 = std::make_shared<MySetProvider>(
      StatefulKeyCompare(true), StatefulKeyHash(12345), StatefulKeyEqual(0));

  // Sets using this provider will be sorted in descending order.
  std::shared_ptr<MySetProvider> setProvider2 = std::make_shared<MySetProvider>(
      StatefulKeyCompare(false), StatefulKeyHash(654321), StatefulKeyEqual(0));

  // Maps using this provider will be sorted in ascending order.
  size_t counter1 = 0;
  size_t counter2 = 0;
  std::shared_ptr<MyMapProvider> mapProvider1 = std::make_shared<MyMapProvider>(
      StatefulMappedHash(&counter1), StatefulMappedEqual(&counter2),
      setProvider1);

  // Maps using this provider will be sorted in descending order.
  size_t counter3 = 0;
  size_t counter4 = 0;
  std::shared_ptr<MyMapProvider> mapProvider2 = std::make_shared<MyMapProvider>(
      StatefulMappedHash(&counter3), StatefulMappedEqual(&counter4),
      setProvider2);

  MySet s1(setProvider1);  // Ascending order.
  MySet s2(setProvider2);  // Descending order.
  MyMap m1(mapProvider1);  // Ascending order.
  MyMap m2(mapProvider2);  // Descending order.

  assert(s1.provider() != s2.provider());
  assert(m1.provider() != m2.provider());
  assert(s1.provider() == m1.provider()->set_provider());
  assert(s2.provider() == m2.provider()->set_provider());

  MySet s3 = s1;  // s3 will get the same set_provider as s1.
  MyMap m3 = m1;  // m3 will get the same map_provider as m1.

  s1 = {1, 2, 3};
  s2 = {1, 2, 3};

  // s1: { 1, 2, 3, }
  std::cout << "s1: { ";
  for (int x : s1)
    std::cout << x << ", ";
  std::cout << "} " << std::endl;

  // s2: { 3, 2, 1, }
  std::cout << "s2: { ";
  for (int x : s2)
    std::cout << x << ", ";
  std::cout << "} " << std::endl;

  m1 = {{1, "a"}, {2, "b"}, {3, "c"}};
  m2 = {{1, "a"}, {2, "b"}, {3, "c"}};

  // m1: { {1, "a"}, {2, "b"}, {3, "c"}, }
  std::cout << "m1: { ";
  for (auto x : m1)
    std::cout << "{" << x.first << ", \"" << x.second << "\"}, ";
  std::cout << "} " << std::endl;

  // m2: { {3, "c"}, {2, "b"}, {1, "a"}, }
  std::cout << "m2: { ";
  for (auto x : m2)
    std::cout << "{" << x.first << ", \"" << x.second << "\"}, ";
  std::cout << "} " << std::endl;

  // Print number of functor calls:
  std::cout << "counters: " << setProvider1->key_eq().count << ", "
            << setProvider2->key_eq().count << ", " << counter1 << ", "
            << counter2 << ", " << counter3 << ", " << counter4 << std::endl;
}
