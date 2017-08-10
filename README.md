# Confluent Sets and Maps #

Confluent sets and maps are sorted associative containers written in C++11.

Containers can be merged at optimal cost in time and memory both when one of
the input containers is small and when the symmetric difference is small.

Sets of the same type share equal nodes. Maps share key nodes with sets
of compaitble type and value nodes with other maps of the same type.

Inserted elements must be comparable, hashable and copy-constructible.

## Source code ##

You find the current source code on [github][1].

~~~~
git clone https://github.com/liljenzin/confluent
~~~~


## Documentation ##

You find the current API documentation and code examples [here][2].

The used merge algorithms are documented [here][3].

## Performance characteristics ##

~~~~
#include "set.h"

confluent::set<int> A = { a_1, a_2, ... };
confluent::set<int> B = { b_1, b_2, ... };

confluent::set<int> C = A;  // copy construction

confluent::set<int> D = A | B;  // set union
confluent::set<int> E = A & B;  // set intersection
confluent::set<int> F = A - B;  // set difference
confluent::set<int> G = A ^ B;  // set symmetric difference

confluent::set<int> H(A.begin() + offset1, A.begin() + offset2);

A.insert(k1);
A.erase(k2);

if (A == B) then { ... };  // Tests that A and B contain the same elements.
size_t h = hash(A):  // Returns the combined hash value of all elements in A.

for (int i : A) { ... }

size_t u = std::max(A.size(), B.size());
size_t v = std::min(A.size(), B.size(), (A ^ B).size());
~~~~

(1) Constructing the sets A and B from ranges is O(n) if input is sorted, otherwise O(n*log n).

(2) Constructing the set C as a copy of another set is constant in time and memory.

(3) Constructing the sets D, E, F and G by merging two sets is O(v*log(u/v)).

(4) Constructing the set H as a slice of another set is O(log(n)).

(5) Inserting or erasing individual keys is O(log n).

(6) Testing if two sets contain the same elements or accessing the combined hash
value of all elements in a set is contant in time.

(7) Iterating all elements in a set is O(n) amortized time.


~~~~
#include <string>
#include "map.h"

confluent::map<int, std::string> A = { a_1, a_2, ... };
confluent::map<int, std::string> B = { b_1, b_2, ... };

confluent::map<int, std::string> C = A;  // copy construction
confluent::set<int> D = A.key_set();  // extracts the set of keys in the map

// The union of two maps will create a new map where elements are unique with
// respect to keys, with precedence for elements from the lhs input map.
confluent::map<int, std::string> E = A | B;  // union

// The union or intersection of two maps will create a new map as if they were
// sets of the value_type, i.e. both keys and mapped values will be matched in
// the merge operations.
confluent::map<int, std::string> F = A & B;  // intersection
confluent::map<int, std::string> G = A - B;  // difference

// The union or intersection of a map and a set will create a new map, while
// only the keys will be matched in the merge operations.
confluent::map<int, std::string> H = A & B.key_set();
confluent::map<int, std::string> I = A - B.key_set();  // difference

confluent::set<int> J(A.begin() + offset1, A.begin() + offset2);

A.insert(v1);
A.insert_or_assign(v2);
A.erase(k1);
A.erase(k2, m2);

std::string m = A.at(k3);

if (A == B) then { ... };  // Tests that A and B contain the same elements.
size_t h = hash(A):  // Returns the combined hash value of all elements in A.

for (const std::pair<int , std::string>& value : A) { ... }

size_t u = std::max(A.size(), B.size());
size_t v = std::min(A.size(), B.size(), (A-B).size() + (B-A).size());
size_t w = std::min(A.size(), B.size(), (A.key_set() ^ B.key_set()).size());
~~~~

(1) Constructing the maps A and B from ranges is O(n) if input is sorted, otherwise O(n*log n).

(2) Constructing the map C as a copy of another map is constant in time and memory.

(3) Constructing the map D containing the keys in a map is constant in time and memory.

(4) Constructing the maps E, F and G by merging two maps is O(v*log(u/v)).

(5) Constructing the maps H and I by merging a map and a set is is O(w*log(u/w)).

(6) Constructing the map J as a slice of another map is O(log(n)).

(7) Inserting or erasing individual keys or values is O(log n).

(8) Testing if two maps contain the same elements or accessing the combined hash
value of all elements in a map is contant in time.

(9) Iterating all elements in a map is O(n) amortized time.


## Applications ##

Confluent sets and maps are powerful alternatives to the standard counterparts
in applications that work with many containers of the same type, in particular
when containers of different size and/or containing overlapping content have to
be merged.


### Persistent data models ###

Confluent sets and maps are backed by immutable trees and keeping copies of
previous versions when containers are modified adds no extra cost in time.

~~~~
typedef confluent::set<int> Set;

std::vector<Set> history;
Set actual_data;

actual_data.insert(1);
history.push_back(actual_data);

actual_data.insert(2);
history.push_back(actual_data);

actual_data.insert(3);
history.push_back(actual_data);
~~~~


### Using containers as keys in hash tables ###

Confluent sets and maps can be copied, hashed and tested for equality in
constant time and can therefore be used as key types in generic hash tables
such as std::unordered_set and std::unordered_map.

~~~~
typedef confluent::set<int> Set;

struct Hasher {
  size_t operator()(const Set& set) const { return set.hash(); }
};

std::unordered_map<Set, std::string, Hasher> ht;

Set s1 = { 1, 2, 3 };
Set s2 = { 2, 3 };

ht[s1] = "a mapped value";
ht[s2] = "another mapped value";
~~~~


### Three-way-merge ###

Confluent maps can be used to implement efficient three-way-merge of
dictionaries based on simple set operations, eleminating the need for
complicated code and suplementary structures often used to keep track of
changes.

A three-way-merge is an operation that merges the changes in two braches
created from a common ancestor, like push and pull operations in git.

Provided a common ancestor A and two branches B and C, if the branches contain
no conflicts, the following function will produce a new container that applies
the changes in both branches:
~~~~
typedef confluent::map<Key, T> map;

map merge(map A, map B, map C) {
	return (A - (A - B) - (A - C)) | (B - A) | (C - A);
}
~~~~
Let n be the total number of elements and let m be the number of changes in the
branches.

The cost of the merge is O(m*log(n/m)), which is optimal for a sorted index
structure.

The merge can easily be extended to also handle conflicts, which is
demonstrated with complete code in the example program [PhoneNumbers.cc][4].

[1]: https://github.com/liljenzin/confluent
[2]: https://www.liljenzin.se/confluent
[3]: https://arxiv.org/abs/1301.3388
[4]: https://www.liljenzin.se/confluent/PhoneNumbers_8cc-example.html
