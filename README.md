Confluent Sets and Maps
=======================

Confluent sets and maps are sorted associative containers written in C++-11.

Sets of the same type share equal nodes. Maps share key nodes with sets
of compaitble type and value nodes with other maps of the same type.

Inserted elements must be comparable, hashable and copy-constructible.

Source code
-----------

You find the current source code on [github][1].

~~~~
git clone https://github.com/liljenzin/confluent
~~~~


Documentation
-------------

You find the current API documentation [here][2].

Core algorithms are documented [here][3].

Performance characteristics
---------------------------

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
size_t h = hash(A):  // Accesses the combined hash value of all elements in A.

for (int i : A) { ... }

size_t u = std::max(A.size(), B.size());
size_t v = std::min(A.size(), B.size(), (A ^ B).size());
~~~~

(1) Constructing the sets A and B from ranges is O(n) if input is sorted, otherwise O(n*log n).

(2) Constructing the set C as a copy of another set is constant in time and memory.

(3) Constructing the sets D, E, F and G by merging two sets is O(v*log(u/v)).

(4) Constructing the set H as a slice of another set is O(log(n)) expected time.

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
size_t h = hash(A):  // Accesses the combined hash value of all elements in A.

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

(6) Constructing the map J as a slice of another map is O(log(n)) expected time.

(7) Inserting or erasing individual keys or values is O(log n).

(8) Testing if two maps contain the same elements or accessing the combined hash
value of all elements in a map is contant in time.

(9) Iterating all elements in a map is O(n) amortized time.



[1]: https://github.com/liljenzin/confluent
[2]: https://www.liljenzin.se/confluent
[3]: https://arxiv.org/abs/1301.3388
