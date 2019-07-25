/*
 * Copyright (c) 2017 Olle Liljenzin
 */

#ifndef CONFLUENT_MAP_H_INCLUDED
#define CONFLUENT_MAP_H_INCLUDED

#include <stdexcept>

#include "set.h"

namespace confluent {

/// @cond HIDDEN_SYMBOLS

template <class Key,
          class T,
          class Compare,
          class Hash,
          class Equal,
          class MappedHash,
          class MappedEqual>
class map_provider;

template <class Key,
          class T,
          class Compare,
          class Hash,
          class Equal,
          class MappedHash,
          class MappedEqual>
class map;

namespace internal {

inline size_t hash_combine(size_t h1, size_t h2, size_t h3, size_t h4) {
  return hash_combine(hash_combine(h1, h2), hash_combine(h3, h4));
}

struct map_tag {};

template <class Traits>
node_ptr<Traits> make_node(const env<Traits, map_tag>& env,
                           const typename Traits::value_type& value,
                           node_ptr<typename Traits::key_set_traits> key_node,
                           node_ptr<Traits> left,
                           node_ptr<Traits> right) {
  return node<Traits>::create(env, value, std::move(key_node), std::move(left),
                              std::move(right), env.hash(value.second));
}

template <class Traits>
node_ptr<Traits> make_node(const env<Traits, map_tag>& env,
                           const typename Traits::value_type& value,
                           node_ptr<Traits> left = nullptr,
                           node_ptr<Traits> right = nullptr) {
  node_ptr<typename Traits::key_set_traits> key_node = make_node(
      env.key_set_env_, value.first, left ? left->key_node() : nullptr,
      right ? right->key_node() : nullptr);
  return make_node(env, value, std::move(key_node), std::move(left),
                   std::move(right));
}

template <class Traits>
node_ptr<Traits> make_node(const env<Traits, map_tag>& env,
                           const node<Traits>& parent,
                           node_ptr<Traits> left,
                           node_ptr<Traits> right) {
  node_ptr<typename Traits::key_set_traits> key_node = make_node(
      env.key_set_env_, *parent.key_node(), left ? left->key_node() : nullptr,
      right ? right->key_node() : nullptr);
  return make_node(env, parent.value(), std::move(key_node), std::move(left),
                   std::move(right));
}

template <class Traits, class Left, class Right>
node_ptr<Traits> set_intersection(const env<Traits>& env,
                                  Left&& left,
                                  Right&& right) {
  if (!left || !right)
    return nullptr;
  if (left->key_node() == right)
    return std::forward<Left>(left);
  switch (rank(env.key_set_env_, *left->key_node(), *right)) {
    case ranking::LEFT: {
      auto s = split(env.key_set_env_, std::forward<Right>(right), left->key());
      return join(env, set_intersection(env, left->left_, std::move(s.first)),
                  set_intersection(env, left->right_, std::move(s.second)));
    }
    case ranking::RIGHT: {
      auto s = split(env, std::forward<Left>(left), right->key());
      return join(env, set_intersection(env, std::move(s.first), right->left_),
                  set_intersection(env, std::move(s.second), right->right_));
    }
    default: {
      return make_node(env, *left,
                       set_intersection(env, left->left_, right->left_),
                       set_intersection(env, left->right_, right->right_));
    }
  }
}

template <class Traits, class Left, class Right>
node_ptr<Traits> set_difference(const env<Traits>& env,
                                Left&& left,
                                Right&& right) {
  if (!left || left->key_node() == right)
    return nullptr;
  if (!right)
    return std::forward<Left>(left);
  switch (rank(env.key_set_env_, *left->key_node(), *right)) {
    case ranking::LEFT: {
      auto s = split(env.key_set_env_, std::forward<Right>(right), left->key());
      return make_node(env, *left,
                       set_difference(env, left->left_, std::move(s.first)),
                       set_difference(env, left->right_, std::move(s.second)));
    }
    case ranking::RIGHT: {
      auto s = split(env, std::forward<Left>(left), right->key());
      return join(env, set_difference(env, std::move(s.first), right->left_),
                  set_difference(env, std::move(s.second), right->right_));
    }
    default: {
      return join(env, set_difference(env, left->left_, right->left_),
                  set_difference(env, left->right_, right->right_));
    }
  }
}

template <class Traits, class NodePtr>
size_t update(const env<Traits>& env, node_ptr<Traits>* p, NodePtr&& q) {
  size_t n = size(*p);
  *p = set_union(env, std::forward<NodePtr>(q), *p);
  return size(*p) - n;
}

template <class Traits, class NodePtr>
size_t diff(const env<Traits>& env, node_ptr<Traits>* p, NodePtr&& q) {
  size_t n = size(*p);
  *p = set_difference(env, *p, std::forward<NodePtr>(q));
  return n - size(*p);
}

template <class Traits, class NodePtr>
size_t intersect(const env<Traits>& env, node_ptr<Traits>* p, NodePtr&& q) {
  size_t n = size(*p);
  *p = set_intersection(env, *p, std::forward<NodePtr>(q));
  return n - size(*p);
}

template <class Traits, class NodePtr>
bool insert_or_assign_n(const env<Traits>& env,
                        node_ptr<Traits>* p,
                        NodePtr&& q) {
  node_ptr<Traits> r = set_union(env, std::forward<NodePtr>(q), *p);
  p->swap(r);
  return *p != r;
}

template <class Traits>
bool insert_or_assign(const env<Traits>& env,
                      node_ptr<Traits>* p,
                      const typename Traits::value_type& value) {
  return insert_or_assign_n(env, p, make_node(env, value));
}

template <class Traits, class InputIterator>
bool insert_or_assign(const env<Traits>& env,
                      node_ptr<Traits>* p,
                      InputIterator first,
                      InputIterator last) {
  return insert_or_assign_n(env, p, make_node(env, first, last));
}

template <class Key,
          class T,
          class Compare,
          class Hash,
          class Equal,
          class MappedHash,
          class MappedEqual>
struct map_traits {
  typedef map_tag category;
  typedef Key key_type;
  typedef T mapped_type;
  typedef std::pair<Key, T> value_type;
  typedef map_provider<Key, T, Compare, Hash, Equal, MappedHash, MappedEqual>
      provider;
  typedef map<Key, T, Compare, Hash, Equal, MappedHash, MappedEqual> container;
  typedef set_traits<Key, Compare, Hash, Equal> key_set_traits;
};

template <class Traits>
struct node<Traits, map_tag> {
  typedef node_ptr<Traits> ptr_type;
  typedef typename Traits::key_type key_type;
  typedef typename Traits::mapped_type mapped_type;
  typedef typename Traits::value_type value_type;
  typedef node_ptr<typename Traits::key_set_traits> key_node_ptr;
  typedef env<Traits> env_type;

  node(const value_type& value,
       key_node_ptr key_node,
       ptr_type left,
       ptr_type right,
       size_t hash)
      : reference_count_(1),
        value_(value),
        key_node_(std::move(key_node)),
        hash_(hash),
        left_(std::move(left)),
        right_(std::move(right)) {}

  static ptr_type create(const env_type& env,
                         const value_type& value,
                         key_node_ptr key_node,
                         ptr_type left,
                         ptr_type right,
                         size_t mapped_hash) {
    size_t hash = hash_combine(internal::hash(left), internal::hash(right),
                               mapped_hash, key_node->hash_);
    std::unique_ptr<node> p(new node(value, std::move(key_node),
                                     std::move(left), std::move(right), hash));
    return get_unique_node(env, std::move(p));
  }

  const key_type& key() const { return value_.first; }
  const mapped_type& mapped() const { return value_.second; }
  const value_type& value() const { return value_; }
  size_t priority() const { return key_node_->priority(); }
  size_t size() const { return key_node_->size(); }
  const key_node_ptr& key_node() const { return key_node_; }

  std::atomic<size_t> reference_count_;
  node* next_;
  value_type value_;
  key_node_ptr key_node_;
  const size_t hash_;
  const ptr_type left_;
  const ptr_type right_;
};

template <class Traits>
struct env<Traits, map_tag> : env_base<Traits> {
  typedef typename Traits::provider provider_type;
  typedef typename Traits::key_type key_type;
  typedef typename Traits::mapped_type mapped_type;
  typedef typename Traits::value_type value_type;
  typedef typename Traits::key_set_traits key_set_traits;
  typedef env<key_set_traits> key_set_env_type;

  using env_base<Traits>::provider_;

  env(provider_type* provider)
      : env_base<Traits>(provider),
        key_set_env_(provider->set_provider().get()) {}

  static bool compare(const key_type& lhs, const key_type& rhs) {
    return key_set_env_type::compare(lhs, rhs);
  }

  static bool compare(const value_type& lhs, const key_type& rhs) {
    return key_set_env_type::compare(lhs.first, rhs);
  }

  static bool compare(const value_type& lhs, const value_type& rhs) {
    return compare(lhs.first, rhs.first);
  }

  static bool equal(const mapped_type& lhs, const mapped_type& rhs) {
    return provider_->mapped_eq()(lhs, rhs);
  }

  static bool equal(const value_type& lhs, const key_type& rhs) {
    return key_set_env_type::equal(lhs.first, rhs);
  }

  static bool equal(const value_type& lhs, const value_type& rhs) {
    return key_set_env_type::equal(lhs.first, rhs.first) &&
           provider_->mapped_eq()(lhs.second, rhs.second);
  }

  static size_t hash(const mapped_type& mapped) {
    return provider_->mapped_hash()(mapped);
  }

  const key_set_env_type key_set_env_;
};

}  // namespace internal

/// @endcond HIDDEN_SYMBOLS

/**
 * A map_provider extends a set_provider with additional resources needed by
 * maps.
 *
 * If not specified when creating a new map, the created map will use a global
 * instance of the provider, otherwise the specified provider will be used.
 * Binary map operations require both input maps to be using the same provider,
 * if not the result is undefined. Operations that takes a map and a set as
 * input requires both containers to use the same set_provider, if not the
 * result is undefined. New maps that are created as a result of map operations
 * will use the same provider as the parent map(s).
 *
 * A map_provider should be owned by a std::shared_ptr and it is recommended to
 * use the helper function std::make_shared<map_provider<>>() to instantiate
 * new providers.
 **/
template <class Key,
          class T,
          class Compare = std::less<Key>,
          class Hash = std::hash<Key>,
          class Equal = std::equal_to<Key>,
          class MappedHash = std::hash<T>,
          class MappedEqual = std::equal_to<T>>
class map_provider {
  typedef internal::
      map_traits<Key, T, Compare, Hash, Equal, MappedHash, MappedEqual>
          traits;

  friend struct internal::env_base<traits>;

 public:
  typedef confluent::set_provider<Key, Compare, Hash, Equal> set_provider_type;

  /**
   * Constructs a new map_provider.
   *
   * @param mapped_hash hash function for computing hash values of mapped
   *     elements
   * @param mapped_equal comparison function that tests if mapped elements are
   *     equal
   * @param set_provider set_provider that will be extended by this map_provider
   **/
  map_provider(const MappedHash& mapped_hash = MappedHash(),
               const MappedEqual& mapped_equal = MappedEqual(),
               const std::shared_ptr<set_provider_type>& set_provider =
                   set_provider_type::default_provider())
      : mapped_hash_(mapped_hash),
        mapped_equal_(mapped_equal),
        set_provider_(set_provider) {
    assert(set_provider_);
  }

  map_provider(const map_provider&) = delete;

  ~map_provider() { assert(size() == 0); }

  /**
   * Returns the hash function for mapped values.
   **/
  const MappedHash& mapped_hash() const { return mapped_hash_; }

  /**
   * Returns the comparison function that tests if mapped elements are equal.
   **/
  const MappedEqual& mapped_eq() const { return mapped_equal_; }

  /**
   * Returns the set_proivder this map_provider extends.
   **/
  const std::shared_ptr<set_provider_type>& set_provider() const {
    return set_provider_;
  }

  /**
   * Returns the number of nodes allocated by this provider.
   **/
  size_t size() const {
    std::lock_guard<std::mutex> lock(hash_table_.mutex_);
    return hash_table_.size_;
  }

  /**
   * Returns a shared pointer to the default instance.
   **/
  static const std::shared_ptr<map_provider>& default_provider() {
    static const std::shared_ptr<map_provider> provider =
        std::make_shared<map_provider>();
    return provider;
  }

 private:
  const MappedHash mapped_hash_;
  const MappedEqual mapped_equal_;
  const std::shared_ptr<set_provider_type> set_provider_;
  internal::hash_table<traits> hash_table_;
};

/**
 * The class confluent::map is a sorted associative container whose instances
 * share key nodes with other sets and maps using the same set_provider and
 * share assigment nodes with other maps using the same map_proivder.
 *
 * Cloning maps and testing maps for equal content run in constant time. Merge
 * operations such as set union, set intersection and set difference perform at
 * optimal cost not only when one of the input maps is smaller than the other,
 * but also when the number of elements that differ between the input maps is
 * small. Maps can also be merged with sets at the same cost as maps can be
 * merged with maps in operations that remove elements, such as set intersection
 * and set difference.
 *
 * Contained elements must be comparable, hashable and copy-constructible and
 * documented complexity is based on that such operations are constant in time
 * and memory and that hash collisions are rare.
 */
template <class Key,
          class T,
          class Compare = std::less<Key>,
          class Hash = std::hash<Key>,
          class Equal = std::equal_to<Key>,
          class MappedHash = std::hash<T>,
          class MappedEqual = std::equal_to<T>>
class map {
  typedef internal::
      map_traits<Key, T, Compare, Hash, Equal, MappedHash, MappedEqual>
          traits;
  typedef internal::set_traits<Key, Compare, Hash, Equal> key_set_traits;
  typedef internal::env<traits> env_type;

 public:
  typedef Key key_type;
  typedef T mapped_type;
  typedef std::pair<Key, T> value_type;
  typedef set<Key, Compare, Hash, Equal> key_set_type;
  typedef map_provider<Key, T, Compare, Hash, Equal, MappedHash, MappedEqual>
      provider_type;
  typedef std::shared_ptr<provider_type> provider_ptr;
  typedef confluent::iterator<traits> iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;

 private:
  typedef typename key_set_type::provider_type set_provider_type;
  typedef typename internal::node<traits> node_type;
  typedef typename internal::node<key_set_traits> key_node_type;

  friend struct confluent::iterator<traits>;

 public:
  /**
   * Creates a new map.
   *
   * @param provider map_provider to use for this map (optional)
   *
   * Complexity: Constant in time and memory.
   **/
  map(provider_ptr provider = provider_type::default_provider())
      : provider_(std::move(provider)) {}

  /**
   * Creates a new map from a range of elements.
   *
   * @param first range start
   * @param last range end
   * @param provider map_provider to use for this map (optional)
   *
   * Complexity: O(n log n) expected time on random input. O(n) on presorted
   * input. O(n) in memory.
   **/
  template <class InputIterator>
  map(InputIterator first,
      InputIterator last,
      provider_ptr provider = provider_type::default_provider())
      : provider_(std::move(provider)) {
    insert(first, last);
  }

  /**
   * Creates a new map from an initializer_list.
   *
   * @param ilist the list of elements to include in the created map
   * @param provider map_provider to use for this map (optional)
   *
   * Complexity: O(n log n) expected time on random input. O(n) on presorted
   * input. O(n) in memory.
   **/
  map(std::initializer_list<value_type> ilist,
      provider_ptr provider = provider_type::default_provider())
      : provider_(std::move(provider)) {
    insert(ilist);
  }

  /**
   * Creates a new map from a range in another map.
   *
   * The created map will use the same map_provider as the source map.
   *
   * @param first range start
   * @param last range end
   *
   * Complexity: O(log n * log n) expected time and memory.
   **/
  map(iterator first, iterator last) : map(*first.container_) {
    retain(first, last);
  }

  /**
   * Creates a new map as a copy of another map.
   *
   * The created map will use the same map_provider as the source map.
   *
   * @param other other map
   *
   * Complexity: Constant in time and memory.
   **/
  map(const map& other) : provider_(other.provider_), node_(other.node_) {}

  /**
   * Creates a new map by moving content from another map.
   *
   * The created map will use the same map_provider as the source map.
   *
   * Result is undefined if the other map is used after content has been moved.
   *
   * @param other other map
   *
   * Complexity: Constant in time and memory.
   **/
  map(map&& other)
      : provider_(std::move(other.provider_)), node_(std::move(other.node_)) {}

  ~map() { clear(); }

  /**
   * Inserts an element into this map.
   *
   * The new element is inserted if it is not contained before.
   *
   * @param value element to insert
   * @return the number of inserted elements
   *
   * Complexity: O(log n) expected time and memory.
   **/
  size_t insert(const value_type& value) {
    return internal::insert(env(), &node_, value);
  }

  /**
   * Inserts a range of elements into this map.
   *
   * New element are inserted if they are not contained before.
   *
   * @param first range start
   * @param last range end
   * @return the number of inserted elements
   *
   * Complexity: Same cost as first creating a map from the given range and
   *     then inserting the created map into this map.
   **/
  template <class InputIterator>
  size_t insert(InputIterator first, InputIterator last) {
    return internal::insert(env(), &node_, first, last);
  }

  /**
   * Inserts elements from an initializer_list into this map.
   *
   * New element are inserted if they are not contained before.
   *
   * @param ilist the list of elements to insert
   * @return the number of inserted elements
   *
   * Complexity: Same cost as first creating a map from the given
   *     initializer_list and then inserting the created map into this map.
   **/
  size_t insert(std::initializer_list<value_type> ilist) {
    return internal::insert(env(), &node_, ilist.begin(), ilist.end());
  }

  /**
   * Inserts elements in another map into this map.
   *
   * New element are inserted, replacing any contained element with same key.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param other other map to insert elements from
   * @return the number of inserted elements
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   **/
  size_t insert(const map& other) {
    check(other);
    return internal::add(env(), &node_, other.node_);
  }

  /**
   * Inserts an element into this map.
   *
   * The new element is inserted, replacing any contained element with same
   * key.
   *
   * @param value element to insert
   * @return true if the set was updated, false otherwise
   *
   * Complexity: O(log n) expected time and memory.
   **/
  bool insert_or_assign(const value_type& value) {
    return internal::insert_or_assign(env(), &node_, value);
  }

  /**
   * Inserts a range of elements into this map.
   *
   * The new element are inserted, replacing any contained elements with same
   * keys.
   *
   * @param first range start
   * @param last range end
   * @return true if the set was updated, false otherwise
   *
   * Complexity: Same cost as first creating a map from the given range and
   *     then inserting the created map into this map.
   **/
  template <class InputIterator>
  bool insert_or_assign(InputIterator first, InputIterator last) {
    return internal::insert_or_assign(env(), &node_, first, last);
  }

  /**
   * Inserts elements from an initializer_list into this map.
   *
   * The new element are inserted, replacing any contained elements with same
   * keys.
   *
   * @param ilist the list of elements to insert
   * @return true if the set was updated, false otherwise
   *
   * Complexity: Same cost as first creating a map from the given
   *     initializer_list and then inserting the created map into this map.
   **/
  bool insert_or_assign(std::initializer_list<value_type> ilist) {
    return internal::insert_or_assign(env(), &node_, ilist.begin(),
                                      ilist.end());
  }

  /**
   * Inserts elements in another map into this map.
   *
   * The new element are inserted, replacing any contained elements with same
   * keys.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param other other map to insert elements from
   * @return true if the set was updated, false otherwise
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   **/
  bool insert_or_assign(const map& other) {
    check(other);
    return internal::insert_or_assign_n(env(), &node_, other.node_);
  }

  /**
   * Erases an element from this map.
   *
   * An element with the given key is erased if contained in the map.
   *
   * @param key element to erase
   * @return the number of erased elements
   *
   * Complexity: O(log n) expected time and memory.
   **/
  size_t erase(const key_type& key) {
    return internal::erase(env(), &node_, key);
  }

  /**
   * Erases an element from this map.
   *
   * The given element is erased if contained in the map.
   *
   * @param value element to erase
   * @return the number of erased elements
   *
   * Complexity: O(log n) expected time and memory.
   **/
  size_t erase(const value_type& value) {
    return internal::erase(env(), &node_, value);
  }

  /**
   * Erases a range of elements from this map.
   *
   * The given range must be a range in this map.
   *
   * @param first range start
   * @param last range end
   * @return the number of erased elements
   *
   * Complexity: O(log n * log n) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t erase(iterator first, iterator last) {
    check(first, last);
    return internal::erase(env(), &node_, first.pos_, last.pos_);
  }

  /**
   * Erases elements whose keys matches the elements in a given set.
   *
   * After the operation key set of this map will contain the set difference,
   * i.e. all keys that were present in this map but not in the given set.
   *
   * Result is undefined if the given set is not using the same set_provider
   * as the key set of this map.
   *
   * @param other other set to erase elements from
   * @return the number of erased elements
   *
   * Let n be the size of the larger container.
   * Let m be the size of the smaller container.
   * Let d be the size of the difference between the key set of this map and
   * the given set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t erase(const key_set_type& other) {
    check(other);
    return internal::diff(env(), &node_, other.node_);
  }

  /**
   * Erases elements in another map from this map.
   *
   * After the operation this map will contain the map difference, i.e. all
   * elements that were present in this map but not in the other map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param other other map to erase elements from
   * @return the number of erased elements
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t erase(const map& other) {
    check(other);
    return internal::diff(env(), &rank, &node_, other.node_);
  }

  /**
   * Retains a range of elements.
   *
   * The given range must be a range in this map.
   *
   * After the operation this map will contain the elements in the range.
   *
   * @param first range start
   * @param last range end
   * @return the number of erased elements
   *
   * Complexity: O(log n * log n) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t retain(iterator first, iterator last) {
    check(first, last);
    return internal::retain(env(), &node_, first.pos_, last.pos_);
  }

  /**
   * Retains elements whose keys matches the elements in a given set.
   *
   * After the operation key set of this map will contain the set intersection,
   * i.e. all keys that were present in this map and also in the given set.
   *
   * Result is undefined if not the given set is using the same set_provider
   * as the key set of this map.
   *
   * @param other other set to erase elements from
   * @return the number of erased elements
   *
   * Let n be the size of the larger container.
   * Let m be the size of the smaller container.
   * Let d be the size of the difference between the key set of this map and
   * the given set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t retain(const key_set_type& other) {
    check(other);
    return internal::intersect(env(), &node_, other.node_);
  }

  /**
   * Retains elements that are contained in another map.
   *
   * After the operation this map will contain the map intersection, i.e. all
   * elements that were present in this map and also in the other map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param other other map to erase elements from
   * @return the number of erased elements
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t retain(const map& other) {
    check(other);
    return internal::intersect(env(), &rank, &node_, other.node_);
  }

  /**
   * Erases all elements in this map.
   *
   * Complexity: Constant in time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  void clear() {
    if (node_)
      reset(env(), &node_);
  }

  /**
   * Swaps the content of this map with the content of another map.
   *
   * Complexity: Constant in time and memory.
   */
  void swap(map& other) {
    provider_.swap(other.provider_);
    node_.swap(other.node_);
  }

  /**
   * Replaces the content of this map with the content of another map.
   *
   * After the operation this map will use the same provider as the other map.
   *
   * Complexity: Constant in time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map& operator=(const map& other) {
    clear();
    provider_ = other.provider_;
    node_ = other.node_;
    return *this;
  }

  /**
   * Replaces the content of this map with the content of another map.
   *
   * After the operation this map will use the same provider as the other map
   * used before the operation.
   *
   * Result is undefined if the other map is used after content has been moved.
   *
   * Complexity: Constant in time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map& operator=(map&& other) {
    swap(other);
    return *this;
  }

  /**
   * Replaces the content of this map with elements from an initializer_list.
   *
   * @param ilist the list of elements to assign
   *
   * Complexity: O(n log n) expected time on random input. O(n) on presorted
   * input. O(n) in memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map& operator=(std::initializer_list<value_type> ilist) {
    internal::assign(env(), &node_, ilist.begin(), ilist.end());
    return *this;
  }

  /**
   * Returns the union of this map and another map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param rhs other map to merge with this map
   * @return a map containing all elements in this map and in the other map
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   **/
  map operator|(const map& rhs) const {
    check(rhs);
    return map(provider_, set_union(env(), node_, rhs.node_));
  }

  /**
   * Replaces the content of this map with the union of this map and another
   *map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param rhs other map to merge with this map
   * @return a reference to this map after it has been updated
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   **/
  map& operator|=(const map& rhs) {
    check(rhs);
    insert(rhs);
    return *this;
  }

  /**
   * Returns the intersection of this map and another map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param rhs other map to merge with this map
   * @return a map containing all elements in this map and in the other map
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map operator&(const map& rhs) const {
    check(rhs);
    return map(provider_, set_intersection(env(), &rank, node_, rhs.node_));
  }

  /**
   * Returns the intersection of this map and a given set.
   *
   * Result is undefined if not the given set is using the same set_provider
   * as the key set of this map.
   *
   * @param rhs set to merge with this map
   * @return a map containing all elements in this map whose keys are in the
   * given set
   *
   * Let n be the size of the larger container.
   * Let m be the size of the smaller container.
   * Let d be the size of the difference between the key set of this map and
   * the given set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map operator&(const key_set_type& rhs) const {
    check(rhs);
    return map(provider_, set_intersection(env(), node_, rhs.node_));
  }

  /**
   * Replaces the content of this map with the intersection of this map and
   * another map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param rhs other map to merge with this map
   * @return a reference to this map after it has been updated
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map& operator&=(const map& rhs) {
    check(rhs);
    retain(rhs);
    return *this;
  }

  /**
   * Replaces the content of this map with the intersection of this map and
   * a given set.
   *
   * Result is undefined if not the given set is using the same set_provider
   * as the key set of this map.
   *
   * @param rhs other map to merge with this map
   * @return a reference to this map after it has been updated
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map& operator&=(const key_set_type& rhs) {
    check(rhs);
    retain(rhs);
    return *this;
  }

  /**
   * Returns the difference of this map and another map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param rhs other map to merge with this map
   * @return a map containing all elements in this map and in the other map
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map operator-(const map& rhs) const {
    check(rhs);
    return map(provider_, set_difference(env(), &rank, node_, rhs.node_));
  }

  /**
   * Returns the difference of this map and a given set.
   *
   * Result is undefined if not the given set is using the same set_provider
   * as the key set of this map.
   *
   * @param rhs set to merge with this map
   * @return a map containing all elements in this map whose keys are in the
   * given set
   *
   * Let n be the size of the larger container.
   * Let m be the size of the smaller container.
   * Let d be the size of the difference between the key set of this map and
   * the given set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map operator-(const key_set_type& rhs) const {
    check(rhs);
    return map(provider_, set_difference(env(), node_, rhs.node_));
  }

  /**
   * Replaces the content of this map with the difference of this map and
   * another map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param rhs other map to merge with this map
   * @return a reference to this map after it has been updated
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map& operator-=(const map& rhs) {
    check(rhs);
    erase(rhs);
    return *this;
  }

  /**
   * Replaces the content of this map with the intersection of this map and
   * a given set.
   *
   * Result is undefined if not the given set is using the same set_provider
   * as the key set of this map.
   *
   * @param rhs other map to merge with this map
   * @return a reference to this map after it has been updated
   *
   * Let n be the size of the larger map.
   * Let m be the size of the smaller map.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  map& operator-=(const key_set_type& rhs) {
    check(rhs);
    erase(rhs);
    return *this;
  }

  /**
   * Returns an iterator to the beginning of this map.
   **/
  iterator begin() const { return iterator(this, 0); }

  /**
   * Returns an iterator to the end of this map.
   **/
  iterator end() const { return iterator(this, size()); }

  /**
   * Returns an iterator to the beginning of this map.
   **/
  iterator cbegin() const { return begin(); }

  /**
   * Returns an iterator to the end of this map.
   **/
  iterator cend() const { return end(); }

  /**
   * Returns a reverse iterator to the beginning of this map.
   **/
  reverse_iterator rbegin() const { return reverse_iterator(end()); }

  /**
   * Returns a reverse iterator to the end of this map.
   **/
  reverse_iterator rend() const { return reverse_iterator(begin()); }

  /**
   * Finds an element with a given key.
   *
   * @param key key to search for
   * @return an iterator to the found element or end of this set if not found
   *
   * Complexity: O(log n) expected time.
   **/
  iterator find(const key_type& key) const {
    std::pair<const node_type*, size_t> bound = internal::lower_bound(
        node_.get(),
        [&](const node_type& p) { return key_comp(p.key(), key); });
    if (bound.first && key_eq(bound.first->key(), key))
      return iterator(this, bound);
    else
      return end();
  }

  /**
   * Returns an iterator to the first element whose key does not compare less
   * than a given key.
   *
   * @param key key to search for
   * @return an iterator to the first element not less than the given key.
   *
   * Complexity: O(log n) expected time.
   **/
  iterator lower_bound(const key_type& key) const {
    return iterator(this,
                    internal::lower_bound(node_.get(), [&](const node_type& p) {
                      return key_comp(p.key(), key);
                    }));
  }

  /**
   * Returns an iterator to the first element whose key compares greater than a
   * given key.
   *
   * @param key key to search for
   * @return an iterator to the first element greater than the given key.
   *
   * Complexity: O(log n) expected time.
   **/
  iterator upper_bound(const key_type& key) const {
    return iterator(this,
                    internal::lower_bound(node_.get(), [&](const node_type& p) {
                      return !key_comp(key, p.key());
                    }));
  }

  /**
   * Returns range of elements matching a given key.
   *
   * r = s.equal_range(key);
   *
   * is equivalent to
   *
   * r = { s.lower_bound(key), s.upper_bound(key)};
   *
   * Complexity: O(log n) expected time.
   **/
  std::pair<iterator, iterator> equal_range(const key_type& key) const {
    return {lower_bound(key), upper_bound(key)};
  }

  /**
   * Returns a mapped value that matches a given key.
   *
   * @param key key to search for
   * @returns the mapped value of the element whose key matches the given key.
   * @throw std::out_of_range if the given key was not found
   *
   * Complexity: O(log n) expected time.
   **/
  const mapped_type& at(const key_type& key) const {
    const node_type* p =
        internal::lower_bound(node_.get(), [&](const node_type& p) {
          return key_comp(p.key(), key);
        }).first;
    if (!p || !key_eq(p->key(), key))
      throw std::out_of_range("key not found");
    return p->mapped();
  }

  /**
   * Finds an element at a given index.
   *
   * @param k the index of the wanted element
   * @return a reference to the element at the given index
   *
   * Complexity: O(log n) expected time.
   **/
  const value_type& at_index(size_t k) const {
    return internal::at_index(node_.get(), k)->value();
  }

  /**
   * Returns the number of elements whose key match a given key
   *
   * @param key key to search for
   * @returns 0 if an element was found, otherwise 1
   *
   * Complexity: O(log n) expected time.
   **/
  size_t count(const key_type& key) const {
    const key_node_type* p =
        internal::lower_bound(
            node_ ? node_->key_node().get() : nullptr,
            [&](const key_node_type& p) { return key_comp(p.key(), key); })
            .first;
    return p && key_eq(p->key(), key) ? 1 : 0;
  }

  /**
   * Returns the number of elements matching a given key and mapped value.
   *
   * @param key key to search for
   * @param mapped mapped value to search for
   * @returns 0 if an element was found, otherwise 1
   *
   * Complexity: O(log n) expected time.
   **/
  size_t count(const key_type& key, const mapped_type& mapped) const {
    const node_type* p =
        internal::lower_bound(node_.get(), [&](const node_type& p) {
          return key_comp(p.key(), key);
        }).first;
    return p && key_eq(p->key(), key) && mapped_eq(p->mapped(), mapped) ? 1 : 0;
  }

  /**
   * Tests if this map includes the elements in another map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @param other map to test if its elements are included in this map
   * @return true if all elements are included, false otherwise
   *
   * Let n be the size of this map.
   * Let m be the size of the other.
   * Let d be the size of the difference between this map and the other map.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: The operation will return directly if m > n.
   **/
  bool includes(const map& other) const {
    check(other);
    return internal::includes(env(), &map::rank, node_, other.node_);
  }

  /**
   * Returns a set containing the keys in this map.
   *
   * Complexity: Constant in time and memory.
   **/
  key_set_type key_set() const {
    return key_set_type(provider_->set_provider(),
                        node_ ? node_->key_node() : nullptr);
  }

  /**
   * Returns a shared pointer to the map_provider used by this map.
   **/
  const provider_ptr& provider() const { return provider_; }

  /**
   * Tests if this map is empty.
   *
   * @return true if this map contains no elements, false otherwise
   *
   * Complexity: Constant in time.
   **/
  bool empty() const { return !node_; }

  /**
   * Returns the number of elements in this map.
   *
   * Complexity: Constant in time.
   **/
  size_t size() const { return internal::size(node_); }

  /**
   * Returns the combined hash value of all elements in this map.
   *
   * Complexity: Constant in time.
   **/
  size_t hash() const { return internal::hash(node_); }

  /**
   * Tests if this map contains the same elements as another map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @return: true if this map contains the same elements as the other map,
   *     false otherwise
   *
   * Complexity: Constant in time.
   **/
  bool operator==(const map& other) const { return node_ == other.node_; }

  /**
   * Tests if this map does not contain the same elements as another map.
   *
   * Result is undefined if not both maps are using the same map_provider.
   *
   * @return: true if this map does not contain the same elements as the other
   *     map, false otherwise
   *
   * Complexity: Constant in time.
   **/
  bool operator!=(const map& other) const { return node_ != other.node_; }

 private:
  typedef typename internal::node_ptr<traits> node_ptr;

  map(provider_ptr provider, node_ptr node)
      : provider_(std::move(provider)), node_(std::move(node)) {}

  void check(const map& other) const { assert(provider_ == other.provider_); }

  void check(const key_set_type& other) const {
    assert(provider_->set_provider() == other.provider_);
  }

  void check(const iterator& first, const iterator& last) const {
    assert(provider_ == first.container_->provider_);
    assert(provider_ == last.container_->provider_);
    assert(first.pos_ <= last.pos_);
    assert(last.pos_ <= size());
  }

  bool key_comp(const key_type& lhs, const key_type& rhs) const {
    return provider_->set_provider()->key_comp()(lhs, rhs);
  }

  bool value_comp(const value_type& lhs, const value_type& rhs) const {
    return key_comp(lhs.first, rhs.first);
  }

  bool key_eq(const key_type& lhs, const key_type& rhs) const {
    return provider_->set_provider()->key_eq()(lhs, rhs);
  }

  bool mapped_eq(const mapped_type& lhs, const mapped_type& rhs) const {
    return provider_->mapped_eq()(lhs, rhs);
  }

  bool value_eq(const value_type& lhs, const value_type& rhs) const {
    return key_eq(lhs.first, rhs.first) && value_eq(lhs.second, rhs.second);
  }

  static internal::ranking rank(const env_type& e,
                                const node_type& left,
                                const node_type& right) {
    internal::ranking r = internal::rank(e, left, right);
    if (r == internal::ranking::SAME && !e.equal(left.mapped(), right.mapped()))
      return internal::ranking::NOT_SAME;
    return r;
  }

  env_type env() const { return {provider_.get()}; }

  provider_ptr provider_;
  node_ptr node_;
};

/**
 * Swaps content of two maps.
 *
 * swap(x, y);
 *
 * is equivalent to
 *
 * x.swap(y);
 **/
template <class T,
          class Compare,
          class Hash,
          class Equal,
          class MappedHash,
          class MappedEqual>
void swap(map<T, Compare, Hash, Equal, MappedHash, MappedEqual>& x,
          map<T, Compare, Hash, Equal, MappedHash, MappedEqual>& y) {
  x.swap(y);
}

/**
 * Returns the combined hash value of a map.
 *
 * hash(x);
 *
 * is equivalent to
 *
 * x.hash();
 **/
template <class T,
          class Compare,
          class Hash,
          class Equal,
          class MappedHash,
          class MappedEqual>
size_t hash(const map<T, Compare, Hash, Equal, MappedHash, MappedEqual>& x) {
  return x.hash();
}

}  // namespace confluent

#endif  // CONFLUENT_MAP_H_INCLUDED
