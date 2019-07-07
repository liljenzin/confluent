/*
 * Copyright (c) 2017 Olle Liljenzin
 */

#ifndef CONFLUENT_SET_H_INCLUDED
#define CONFLUENT_SET_H_INCLUDED

#include <atomic>
#include <cassert>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace confluent {

/// @cond HIDDEN_SYMBOLS

template <class T, class Compare, class Hash, class Equal>
class set_provider;

template <class T, class Compare, class Hash, class Equal>
class set;

template <class Key,
          class T,
          class Compare,
          class Hash,
          class Equal,
          class MappedHash,
          class MappedEqual>
class map;

namespace internal {

// Thomas Wang's 32 bit mix function
inline std::uint32_t intmix(std::uint32_t key) {
  key = ~key + (key << 15);  // key = (key << 15) - key - 1;
  key = key ^ (key >> 12);
  key = key + (key << 2);
  key = key ^ (key >> 4);
  key = key * 2057;  // key = (key + (key << 3)) + (key << 11);
  key = key ^ (key >> 16);
  return key;
}

// Thomas Wang's 64 bit mix function
inline std::uint64_t intmix(std::uint64_t key) {
  key = (~key) + (key << 21);  // key = (key << 21) - key - 1;
  key = key ^ (key >> 24);
  key = (key + (key << 3)) + (key << 8);  // key * 265
  key = key ^ (key >> 14);
  key = (key + (key << 2)) + (key << 4);  // key * 21
  key = key ^ (key >> 28);
  key = key + (key << 31);
  return key;
}

inline size_t hash_combine(size_t h1, size_t h2) {
  return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

inline size_t hash_combine(size_t h1, size_t h2, size_t h3) {
  return hash_combine(hash_combine(h1, h2), h3);
}

template <class Traits, class Category = typename Traits::category>
struct env;

template <class Traits, class Category = typename Traits::category>
struct node;

template <class Traits>
struct node_ptr;

template <class Traits>
struct node_ptr {
  node_ptr() : node_(nullptr) {}
  node_ptr(std::nullptr_t) : node_(nullptr) {}

  explicit node_ptr(node<Traits>* p, bool add_ref = true) : node_(p) {
    if (add_ref && p)
      incref(p);
  }

  node_ptr(const node_ptr& other) : node_(other.node_) {
    if (node_)
      incref(node_);
  }

  node_ptr(node_ptr&& other) : node_(other.node_) { other.node_ = nullptr; }

  ~node_ptr() {
    if (node_ && !decref(node_))
      delete node_;
  }

  node_ptr& operator=(const node_ptr& other) {
    reset(other.node_);
    return *this;
  }

  node_ptr& operator=(node_ptr&& other) {
    if (node_ && !decref(node_))
      delete node_;
    node_ = other.node_;
    other.node_ = nullptr;
    return *this;
  }

  void reset(node<Traits>* p = nullptr, bool add_ref = true) {
    if (node_ != p) {
      if (add_ref && p)
        incref(p);
      if (node_ && !decref(node_))
        delete node_;
      node_ = p;
    }
  }

  void swap(node_ptr& other) { std::swap(node_, other.node_); }

  node<Traits>& operator*() const { return *node_; }
  node<Traits>* operator->() const { return node_; }

  bool operator==(const node_ptr& other) const { return node_ == other.node_; }

  bool operator!=(const node_ptr& other) const { return node_ != other.node_; }

  node<Traits>* get() const { return node_; }

  explicit operator bool() const { return node_; }

  void incref(node<Traits>* p) const {
    p->reference_count_.fetch_add(1, std::memory_order_relaxed);
  }

  size_t decref(node<Traits>* p) const {
    size_t count = p->reference_count_.load(std::memory_order_relaxed);
    while (1) {
      if (count == 1) {
        std::lock_guard<std::mutex> lock(env<Traits>::get_hash_table().mutex_);
        if (p->reference_count_.compare_exchange_strong(
                count, 0, std::memory_order_relaxed)) {
          env<Traits>::get_hash_table().erase(p);
          return 0;
        }
      } else {
        if (p->reference_count_.compare_exchange_weak(
                count, count - 1, std::memory_order_relaxed))
          return 1;
      }
    }
  }

  static const node_ptr null_;

  node<Traits>* node_;
};

template <class Traits>
const node_ptr<Traits> node_ptr<Traits>::null_;

template <class Traits>
struct hash_table {
  hash_table()
      : buckets_(alloc(min_bucket_count_)),
        bucket_count_(min_bucket_count_),
        size_(0) {}

  node<Traits>* insert(node<Traits>* key) {
    rehash();
    node<Traits>** p = &buckets_[pos(key)];
    while (*p) {
      if ((*p)->hash_ == key->hash_ && (*p)->left_ == key->left_ &&
          (*p)->right_ == key->right_ &&
          env<Traits>::equal((*p)->value(), key->value()))
        return *p;
      p = &(*p)->next_;
    }
    *p = key;
    key->next_ = nullptr;
    ++size_;
    return key;
  }

  void erase(node<Traits>* key) {
    node<Traits>** p = &buckets_[pos(key)];
    while (*p != key)
      p = &(*p)->next_;
    *p = key->next_;
    --size_;
  }

  void rehash() {
    if (size_ >= bucket_count_)
      extend();
    else if (size_ > min_bucket_count_ && (size_ << 1) < bucket_count_)
      reduce();
  }

  static std::unique_ptr<node<Traits>* []> alloc(size_t bucket_count) {
    std::unique_ptr<node<Traits>*[]> buckets(new node<Traits>*[bucket_count]);
    std::fill(buckets.get(), buckets.get() + bucket_count, nullptr);
    return buckets;
  }

  size_t pos(node<Traits>* key) const {
    return key->hash_ & (bucket_count_ - 1);
  }

  void extend() {
    bucket_count_ *= 2;
    std::unique_ptr<node<Traits>*[]> new_buckets = alloc(bucket_count_);
    std::fill(new_buckets.get(), new_buckets.get() + bucket_count_, nullptr);
    for (size_t i = 0; i < bucket_count_ / 2; ++i) {
      node<Traits>* head = buckets_[i];
      while (head) {
        node<Traits>* next = head->next_;
        size_t j = pos(head);
        head->next_ = new_buckets[j];
        new_buckets[j] = head;
        head = next;
      }
    }
    swap(buckets_, new_buckets);
  }

  void reduce() {
    bucket_count_ /= 2;
    std::unique_ptr<node<Traits>*[]> new_buckets(
        new node<Traits>*[bucket_count_]);
    for (size_t i = 0; i < bucket_count_; ++i) {
      node<Traits>* low_head = buckets_[i];
      node<Traits>* high_head = buckets_[i + bucket_count_];
      if (low_head) {
        new_buckets[i] = low_head;
        while (low_head->next_)
          low_head = low_head->next_;
        low_head->next_ = high_head;
      } else {
        new_buckets[i] = high_head;
      }
    }
    swap(buckets_, new_buckets);
  }

  // Initial size. Must be power of two.
  static constexpr size_t min_bucket_count_ = 1 << 3;

  mutable std::mutex mutex_;
  std::unique_ptr<node<Traits>*[]> buckets_;
  size_t bucket_count_;
  size_t size_;
};

template <class Traits>
struct env_base {
  env_base(typename Traits::provider* provider) : saved_provider_(provider_) {
    provider_ = provider;
  }

  env_base(const env_base&) = delete;

  ~env_base() { provider_ = saved_provider_; }

  void silence_unused_warning() const {}

  static hash_table<Traits>& get_hash_table() { return provider_->hash_table_; }

  typename Traits::provider* const saved_provider_;

  static thread_local typename Traits::provider* provider_;
};

template <class Traits>
thread_local typename Traits::provider* env_base<Traits>::provider_;

enum ranking { LEFT = -1, SAME = 0, RIGHT = 1, NOT_SAME };

template <class Traits>
size_t hash(const node<Traits>* p) {
  return p ? p->hash_ : 0;
}

template <class Traits>
size_t hash(const node_ptr<Traits>& p) {
  return p ? p->hash_ : 0;
}

template <class Traits>
size_t size(const node<Traits>* p) {
  return p ? p->size() : 0;
}

template <class Traits>
size_t size(const node_ptr<Traits>& p) {
  return p ? p->size() : 0;
}

template <class Traits>
const node<Traits>* at_index(const node<Traits>* p, size_t k) {
  assert(k < size(p));

  while (true) {
    while (k > size(p->left_)) {
      k -= size(p->left_) + 1;
      p = p->right_.get();
    }
    while (k < size(p->left_))
      p = p->left_.get();
    if (k == size(p->left_))
      return p;
  };
}

template <class Traits, class Comparer>
std::pair<const node<Traits>*, size_t> lower_bound(const node<Traits>* p,
                                                   Comparer comparer) {
  std::pair<const node<Traits>*, size_t> best = {nullptr, size(p)};
  size_t pos = 0;

  while (p) {
    if (comparer(*p)) {
      pos += size(p->left_) + 1;
      p = p->right_.get();
    } else {
      best = {p, pos + size(p->left_)};
      p = p->left_.get();
    }
  };
  return best;
}

struct set_tag {};

template <class Traits>
void reset(const env<Traits>& env, node_ptr<Traits>* p) {
  env.silence_unused_warning();
  p->reset();
}

template <class Traits>
node_ptr<Traits> get_unique_node(const env<Traits>& env,
                                 std::unique_ptr<node<Traits>> p) {
  std::lock_guard<std::mutex> lock(env.get_hash_table().mutex_);
  node<Traits>* q = env.get_hash_table().insert(p.get());
  return q == p.get() ? node_ptr<Traits>(p.release(), false)
                      : node_ptr<Traits>(q);
}

template <class Traits>
node_ptr<Traits> make_node(const env<Traits, set_tag>& env,
                           const typename Traits::value_type& value,
                           node_ptr<Traits> left = nullptr,
                           node_ptr<Traits> right = nullptr) {
  return node<Traits>::create(env, value, std::move(left), std::move(right),
                              intmix(env.hash(value)));
}

template <class Traits>
node_ptr<Traits> make_node(const env<Traits, set_tag>& env,
                           const node<Traits>& parent,
                           node_ptr<Traits> left,
                           node_ptr<Traits> right) {
  return node<Traits>::create(env, parent.value(), std::move(left),
                              std::move(right), parent.priority());
}

template <class Traits>
ranking rank(const env<Traits>& env,
             const node<Traits>& left,
             const node<Traits>& right) {
  if (left.priority() < right.priority())
    return LEFT;
  if (right.priority() < left.priority())
    return RIGHT;
  if (env.compare(left.key(), right.key()))
    return LEFT;
  if (env.compare(right.key(), left.key()))
    return RIGHT;
  return SAME;
}

template <class Traits, class Parent, class Child>
node_ptr<Traits> replace_left(const env<Traits>& env,
                              Parent&& parent,
                              Child&& child) {
  if (parent->left_ == child)
    return std::forward<Parent>(parent);
  return make_node(env, *parent, std::forward<Child>(child), parent->right_);
}

template <class Traits, class Parent, class Child>
node_ptr<Traits> replace_right(const env<Traits>& env,
                               Parent&& parent,
                               Child&& child) {
  if (parent->right_ == child)
    return std::forward<Parent>(parent);
  return make_node(env, *parent, parent->left_, std::forward<Child>(child));
}

template <class Traits, class Left, class Right>
node_ptr<Traits> join(const env<Traits>& env, Left&& left, Right&& right) {
  if (!left)
    return std::forward<Right>(right);
  if (!right)
    return std::forward<Left>(left);
  switch (rank(env, *left, *right)) {
    case LEFT:
      return replace_right(env, left,
                           join(env, left->right_, std::forward<Right>(right)));
    case RIGHT:
      return replace_left(env, right,
                          join(env, std::forward<Left>(left), right->left_));

    default:
      /* keys should never compare equal in join() */
      assert(false);
      return nullptr;
  }
}

template <class Traits, class NodePtr>
std::pair<node_ptr<Traits>, node_ptr<Traits>> split(
    const env<Traits>& env,
    NodePtr&& p,
    const typename Traits::key_type& key) {
  if (!p)
    return {};
  if (env.compare(p->key(), key)) {
    auto s = split(env, p->right_, key);
    return {replace_right(env, std::forward<NodePtr>(p), std::move(s.first)),
            std::move(s.second)};
  } else {
    auto s = split(env, p->left_, key);
    return {std::move(s.first),
            replace_left(env, std::forward<NodePtr>(p), std::move(s.second))};
  }
}

template <class Traits, class Left, class Right>
node_ptr<Traits> set_union(const env<Traits>& env, Left&& left, Right&& right) {
  if (left == right || !right)
    return std::forward<Left>(left);
  if (!left)
    return std::forward<Right>(right);
  switch (rank(env, *left, *right)) {
    case LEFT: {
      auto s = split(env, std::forward<Right>(right), left->key());
      return make_node(env, *left,
                       set_union(env, left->left_, std::move(s.first)),
                       set_union(env, left->right_, std::move(s.second)));
    }
    case RIGHT: {
      auto s = split(env, std::forward<Left>(left), right->key());
      return make_node(env, *right,
                       set_union(env, std::move(s.first), right->left_),
                       set_union(env, std::move(s.second), right->right_));
    }
    default: {
      return make_node(env, *left, set_union(env, left->left_, right->left_),
                       set_union(env, left->right_, right->right_));
    }
  }
}

template <class Traits, class Rank, class Left, class Right>
node_ptr<Traits> set_intersection(const env<Traits>& env,
                                  Rank ranker,
                                  Left&& left,
                                  Right&& right) {
  if (!left || !right)
    return nullptr;
  if (left == right)
    return std::forward<Left>(left);
  switch (ranker(env, *left, *right)) {
    case LEFT: {
      auto s = split(env, std::forward<Right>(right), left->key());
      return join(
          env, set_intersection(env, ranker, left->left_, std::move(s.first)),
          set_intersection(env, ranker, left->right_, std::move(s.second)));
    }
    case RIGHT: {
      auto s = split(env, std::forward<Left>(left), right->key());
      return join(
          env, set_intersection(env, ranker, std::move(s.first), right->left_),
          set_intersection(env, ranker, std::move(s.second), right->right_));
    }
    case NOT_SAME: {
      return join(env, set_intersection(env, ranker, left->left_, right->left_),
                  set_intersection(env, ranker, left->right_, right->right_));
    }
    default: {
      return make_node(
          env, *left, set_intersection(env, ranker, left->left_, right->left_),
          set_intersection(env, ranker, left->right_, right->right_));
    }
  }
}

template <class Traits, class Rank, class Left, class Right>
node_ptr<Traits> set_difference(const env<Traits>& env,
                                Rank ranker,
                                Left&& left,
                                Right&& right) {
  if (left == right || !left)
    return nullptr;
  if (!right)
    return std::forward<Left>(left);
  switch (ranker(env, *left, *right)) {
    case LEFT: {
      auto s = split(env, std::forward<Right>(right), left->key());
      return make_node(
          env, *left,
          set_difference(env, ranker, left->left_, std::move(s.first)),
          set_difference(env, ranker, left->right_, std::move(s.second)));
    }
    case RIGHT: {
      auto s = split(env, std::forward<Left>(left), right->key());
      return join(
          env, set_difference(env, ranker, std::move(s.first), right->left_),
          set_difference(env, ranker, std::move(s.second), right->right_));
    }
    case NOT_SAME: {
      return make_node(
          env, *left, set_difference(env, ranker, left->left_, right->left_),
          set_difference(env, ranker, left->right_, right->right_));
    }
    default: {
      return join(env, set_difference(env, ranker, left->left_, right->left_),
                  set_difference(env, ranker, left->right_, right->right_));
    }
  }
}

template <class Traits, class Left, class Right>
node_ptr<Traits> set_symmetric(const env<Traits>& env,
                               Left&& left,
                               Right&& right) {
  if (!left)
    return std::forward<Right>(right);
  if (!right)
    return std::forward<Left>(left);
  if (left == right)
    return nullptr;
  switch (rank(env, *left, *right)) {
    case LEFT: {
      auto s = split(env, std::forward<Right>(right), left->key());
      return make_node(env, *left,
                       set_symmetric(env, left->left_, std::move(s.first)),
                       set_symmetric(env, left->right_, std::move(s.second)));
    }
    case RIGHT: {
      auto s = split(env, std::forward<Left>(left), right->key());
      return make_node(env, *right,
                       set_symmetric(env, std::move(s.first), right->left_),
                       set_symmetric(env, std::move(s.second), right->right_));
    }
    default: {
      return join(env, set_symmetric(env, left->left_, right->left_),
                  set_symmetric(env, left->right_, right->right_));
    }
  }
}

template <class Traits, class Rank, class Left, class Right>
bool includes(const env<Traits>& env, Rank ranker, Left&& left, Right&& right) {
  if (left == right || !right)
    return true;

  if (size(left) < size(right))
    return false;

  switch (ranker(env, *left, *right)) {
    case LEFT: {
      auto s = split(env, std::forward<Right>(right), left->key());
      return includes(env, ranker, left->left_, std::move(s.first)) &&
             includes(env, ranker, left->right_, std::move(s.second));
    }
    case SAME: {
      return includes(env, ranker, left->left_, right->left_) &&
             includes(env, ranker, left->right_, right->right_);
    }
    default: {
      return false;
    }
  }
}

template <class Traits, class InputIterator>
node_ptr<Traits> make_node(const env<Traits>& env,
                           InputIterator* first,
                           InputIterator last,
                           size_t max_depth) {
  if (*first == last)
    return nullptr;
  node_ptr<Traits> root = make_node(env, **first);
  ++*first;
  for (size_t depth = 0; depth < max_depth; ++depth) {
    node_ptr<Traits> branch = make_node(env, first, last, depth);
    if (!branch)
      break;
    root = set_union(env, std::move(root), std::move(branch));
  }
  return root;
}

template <class Traits, class InputIterator>
node_ptr<Traits> make_node(const env<Traits>& env,
                           InputIterator first,
                           InputIterator last) {
  InputIterator it = first;
  return make_node(env, &it, last, std::numeric_limits<size_t>::max());
}

template <class Traits, class NodePtr>
size_t add(const env<Traits>& env, node_ptr<Traits>* p, NodePtr&& q) {
  size_t n = size(*p);
  *p = set_union(env, *p, std::forward<NodePtr>(q));
  return size(*p) - n;
}

template <class Traits, class Rank, class NodePtr>
size_t diff(const env<Traits>& env,
            Rank ranker,
            node_ptr<Traits>* p,
            NodePtr&& q) {
  size_t n = size(*p);
  *p = set_difference(env, ranker, *p, std::forward<NodePtr>(q));
  return n - size(*p);
}

template <class Traits>
size_t erase(const env<Traits>& env,
             node_ptr<Traits>* p,
             size_t first,
             size_t last) {
  size_t n = size(*p);
  *p = join(env, head(env, p, first), tail(env, p, last));
  return n - size(*p);
}

template <class Traits, class Rank, class NodePtr>
size_t intersect(const env<Traits>& env,
                 Rank ranker,
                 node_ptr<Traits>* p,
                 NodePtr&& q) {
  size_t n = size(*p);
  *p = set_intersection(env, ranker, *p, std::forward<NodePtr>(q));
  return n - size(*p);
}

template <class Traits>
size_t insert(const env<Traits>& env,
              node_ptr<Traits>* p,
              const typename Traits::value_type& value) {
  return add(env, p, make_node(env, value));
}

template <class Traits, class InputIterator>
size_t insert(const env<Traits>& env,
              node_ptr<Traits>* p,
              InputIterator first,
              InputIterator last) {
  return add(env, p, make_node(env, first, last));
}

template <class Traits, class InputIterator>
void assign(const env<Traits>& env,
            node_ptr<Traits>* p,
            InputIterator first,
            InputIterator last) {
  *p = make_node(env, first, last);
}

template <class Traits, class Key>
std::pair<node_ptr<Traits>, bool> erase(const env<Traits>& env,
                                        const node_ptr<Traits>& p,
                                        const Key& key) {
  if (!p)
    return {nullptr, false};
  if (env.compare(p->value(), key)) {
    auto s = erase(env, p->right_, key);
    if (s.second)
      return {replace_right(env, p, std::move(s.first)), true};
    return {p, false};
  }
  auto s = erase(env, p->left_, key);
  if (s.second)
    return {replace_left(env, p, std::move(s.first)), true};
  if (!env.equal(p->value(), key))
    return {p, true};
  return {join(env, p->left_, p->right_), true};
}

template <class Traits, class Key>
size_t erase(const env<Traits>& env, node_ptr<Traits>* p, const Key& key) {
  size_t n = size(*p);
  *p = erase(env, *p, key).first;
  return n - size(*p);
}

template <class Traits>
size_t retain(const env<Traits>& env,
              node_ptr<Traits>* p,
              size_t first,
              size_t last) {
  size_t n = size(*p);
  node_ptr<Traits> h = head(env, p, last);
  *p = tail(env, &h, first);
  return n - size(*p);
}

template <class Traits>
void symmetric(const env<Traits>& env,
               node_ptr<Traits>* p,
               const node_ptr<Traits>& q) {
  *p = set_symmetric(env, *p, q);
}

template <class Traits>
node_ptr<Traits> tail(const env<Traits>& env,
                      const node_ptr<Traits>* p,
                      size_t first) {
  while (*p && first > size((*p)->left_)) {
    first -= size((*p)->left_) + 1;
    p = &(*p)->right_;
  }
  return first ? replace_left(env, *p, tail(env, &(*p)->left_, first)) : *p;
}

template <class Traits>
node_ptr<Traits> head(const env<Traits>& env,
                      const node_ptr<Traits>* p,
                      size_t last) {
  while (*p && last <= size((*p)->left_)) {
    p = &(*p)->left_;
  }
  return last == size(*p) ? *p
                          : replace_right(env, *p,
                                          head(env, &(*p)->right_,
                                               last - size((*p)->left_) - 1));
}

template <class T, class Compare, class Hash, class Equal>
struct set_traits {
  typedef set_tag category;
  typedef T key_type;
  typedef T value_type;
  typedef set_provider<T, Compare, Hash, Equal> provider;
  typedef set<T, Compare, Hash, Equal> container;
};

template <class Traits>
struct node<Traits, set_tag> {
  typedef typename Traits::key_type key_type;
  typedef typename Traits::value_type value_type;
  typedef node_ptr<Traits> ptr_type;
  typedef env<Traits> env_type;

  node(const value_type& value,
       size_t priority,
       size_t sz,
       ptr_type left,
       ptr_type right,
       size_t h)
      : reference_count_(1),
        value_(value),
        priority_(priority),
        size_(sz),
        hash_(h),
        left_(std::move(left)),
        right_(std::move(right)) {}

  static ptr_type create(const env_type& env,
                         const value_type& value,
                         ptr_type left,
                         ptr_type right,
                         size_t priority) {
    size_t sz = 1 + internal::size(left) + internal::size(right);
    size_t h = hash_combine(hash(left), hash(right), priority);
    std::unique_ptr<node> p(
        new node(value, priority, sz, std::move(left), std::move(right), h));
    return get_unique_node(env, std::move(p));
  }

  const key_type& key() const { return value_; }
  const value_type& value() const { return value_; }
  size_t priority() const { return priority_; }
  size_t size() const { return size_; }

  std::atomic<size_t> reference_count_;
  node* next_;
  const value_type value_;
  const size_t priority_;
  const size_t size_;
  const size_t hash_;
  const ptr_type left_;
  const ptr_type right_;
};

template <class Traits>
struct env<Traits, set_tag> : env_base<Traits> {
  typedef typename Traits::provider provider_type;
  typedef typename Traits::key_type key_type;
  typedef hash_table<Traits> hash_table_type;

  using env_base<Traits>::env_base;
  using env_base<Traits>::provider_;

  static bool compare(const key_type& lhs, const key_type& rhs) {
    return provider_->key_comp()(lhs, rhs);
  }

  static bool equal(const key_type& lhs, const key_type& rhs) {
    return provider_->key_eq()(lhs, rhs);
  }

  static size_t hash(const key_type& key) { return provider_->key_hash()(key); }
};

}  // namespace internal

template <class Traits>
struct iterator {
  typedef internal::node<Traits> node_type;
  typedef typename Traits::container container_type;

  typedef std::ptrdiff_t difference_type;
  typedef const typename Traits::value_type value_type;
  typedef const value_type* pointer;
  typedef const value_type& reference;
  typedef std::bidirectional_iterator_tag iterator_category;

  iterator() : container_(nullptr), pos_(0), node_(nullptr) {}

  iterator(const container_type* container,
           size_t pos,
           const node_type* node = nullptr)
      : container_(container), pos_(pos), node_(node), decrementing_(false) {}

  iterator(const container_type* container,
           std::pair<const node_type*, size_t> p)
      : container_(container),
        pos_(p.second),
        node_(p.first),
        decrementing_(false) {}

  iterator(const iterator& other)
      : container_(other.container_),
        pos_(other.pos_),
        node_(other.node_),
        decrementing_(false) {}

  reference operator*() const { return find_node()->value(); }
  pointer operator->() const { return &find_node()->value(); }

  iterator& operator++() {
    *this += 1;
    return *this;
  }

  iterator operator++(int) {
    iterator it(container_, pos_, node_);
    *this += 1;
    return it;
  }

  iterator operator+(difference_type k) const {
    return iterator(container_, pos_ + k);
  }

  iterator& operator+=(difference_type k) {
    reset(pos_ + k);
    return *this;
  }

  iterator& operator--() {
    *this -= 1;
    return *this;
  }

  iterator operator--(int) {
    iterator it(container_, pos_, node_);
    *this -= 1;
    return it;
  }

  iterator operator-(difference_type k) const {
    return iterator(container_, pos_ - k);
  }

  iterator& operator-=(difference_type k) {
    reset(pos_ - k);
    return *this;
  }

  void swap(iterator& other) {
    std::swap(container_, other.container_);
    std::swap(pos_, other.pos_);
    std::swap(node_, other.node_);
    std::swap(stack_, other.stack_);
    std::swap(decrementing_, other.decrementing_);
  }

  iterator& operator=(const iterator& other) {
    container_ = other.container_;
    pos_ = other.pos_;
    node_ = other.node_;
    stack_.clear();
    return *this;
  }

  iterator& operator=(iterator&& other) {
    swap(other);
    return *this;
  }

  bool operator==(const iterator& other) const { return pos_ == other.pos_; }
  bool operator!=(const iterator& other) const { return pos_ != other.pos_; }
  bool operator<(const iterator& other) const { return pos_ < other.pos_; }
  bool operator<=(const iterator& other) const { return pos_ <= other.pos_; }
  bool operator>(const iterator& other) const { return pos_ > other.pos_; }
  bool operator>=(const iterator& other) const { return pos_ >= other.pos_; }

  void increment() {
    ++pos_;
    if (decrementing_) {
      stack_.clear();
      decrementing_ = false;
    }
    if (stack_.empty()) {
      size_t k = pos_;
      node_ = container_->node_.get();
      do {
        while (k > size(node_->left_)) {
          k -= size(node_->left_) + 1;
          node_ = node_->right_.get();
        }
        while (k < size(node_->left_)) {
          stack_.push_back(node_);
          node_ = node_->left_.get();
        }
      } while (k != size(node_->left_));
      return;
    }
    if (node_->right_) {
      node_ = node_->right_.get();
      while (node_->left_) {
        stack_.push_back(node_);
        node_ = node_->left_.get();
      }
    } else {
      node_ = stack_.back();
      stack_.pop_back();
    }
  }

  void decrement() {
    --pos_;
    if (!decrementing_) {
      stack_.clear();
      decrementing_ = true;
    }
    if (stack_.empty()) {
      size_t k = pos_;
      node_ = container_->node_.get();
      do {
        while (k > size(node_->left_)) {
          k -= size(node_->left_) + 1;
          stack_.push_back(node_);
          node_ = node_->right_.get();
        }
        while (k < size(node_->left_)) {
          node_ = node_->left_.get();
        }
      } while (k != size(node_->left_));
      return;
    }
    if (node_->left_) {
      node_ = node_->left_.get();
      while (node_->right_) {
        stack_.push_back(node_);
        node_ = node_->right_.get();
      }
    } else {
      node_ = stack_.back();
      stack_.pop_back();
    }
  }

  void reset(size_t pos) {
    if (pos == pos_ + 1 && pos != size(container_->node_)) {
      increment();
      return;
    }
    if (pos == pos_ - 1) {
      decrement();
      return;
    }
    if (pos_ != pos) {
      pos_ = pos;
      node_ = nullptr;
      stack_.clear();
    }
  }

  const node_type* find_node() const {
    assert(pos_ < size(container_->node_));
    if (!node_)
      node_ = at_index(container_->node_.get(), pos_);
    return node_;
  }

  const container_type* container_;
  size_t pos_;
  mutable const node_type* node_;
  std::vector<const node_type*> stack_;
  bool decrementing_;
};

template <class Traits>
std::ptrdiff_t distance(const iterator<Traits>& from,
                        const iterator<Traits>& to) {
  return to.pos_ - from.pos_;
}

/// @endcond HIDDEN_SYMBOLS

/**
 * A set_provider provides resources such as nodes and functors to instances of
 * set and map.
 *
 * If not specified when creating a new set, the created set will use a global
 * instance of the provider, otherwise the specified provider will be used.
 * Binary set operations require both input sets to be using the same provider,
 * if not the result is undefined. New sets that are created as a result of
 * set operations will use the same provider as the parent set(s).
 *
 * All sets and maps using the same provider will share structurally equal
 * nodes. If an element is inserted in one container and another element that
 * compares equal is inserted in another container using the same provider,
 * it is then undefined which of the elements that will be copy constructed into
 * the shared nodes and later found when looking up elements in the containers.
 *
 * New providers can be created to make partitions that cannot share nodes or to
 * create containers with stateful functors that cannot be default constructed
 * and therefore prevents default construction of a global provider instance.
 *
 * A set_provider should be owned by a std::shared_ptr and it is recommended to
 * use the helper function std::make_shared<set_provider<>>() to instantiate new
 * providers.
 **/
template <class T,
          class Compare = std::less<T>,
          class Hash = std::hash<T>,
          class Equal = std::equal_to<T>>
class set_provider {
  typedef internal::set_traits<T, Compare, Hash, Equal> traits;

  friend struct internal::env_base<traits>;

 public:
  /**
   * Constructs a new set_provider.
   *
   * @param compare comparison function that defines sort order
   * @param hash hash function for computing hash values of elements
   * @param equal comparison function that tests if elements are equal
   **/
  set_provider(const Compare& compare = Compare(),
               const Hash& hash = Hash(),
               const Equal& equal = Equal())
      : compare_(compare), hash_(hash), equal_(equal) {}

  set_provider(const set_provider&) = delete;

  ~set_provider() { assert(size() == 0); }

  /**
   * Returns the comparison function that defines sort order.
   **/
  const Compare& key_comp() const { return compare_; }

  /**
   * Returns the hash function.
   **/
  const Hash& key_hash() const { return hash_; }

  /**
   * Returns the comparison function that tests if elements are equal.
   **/
  const Equal& key_eq() const { return equal_; }

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
  static const std::shared_ptr<set_provider>& default_provider() {
    static const std::shared_ptr<set_provider> provider =
        std::make_shared<set_provider>();
    return provider;
  }

 private:
  const Compare compare_;
  const Hash hash_;
  const Equal equal_;
  internal::hash_table<traits> hash_table_;
};

/**
 * The class confluent::set is a sorted associative container whose instances
 * share nodes with other sets and maps using the same set_provider.
 *
 * Cloning sets and testing sets for equal content run in constant time. Merge
 * operations such as set union, set intersection and set difference perform at
 * optimal cost not only when one of the input sets is smaller than the other,
 * but also when the number of elements that differ between the inputs set is
 * small.
 *
 * Contained elements must be comparable, hashable and copy-constructible and
 * documented performance is based on that such operations are constant in time
 * and memory and that hash collisions are rare.
 */
template <class T,
          class Compare = std::less<T>,
          class Hash = std::hash<T>,
          class Equal = std::equal_to<T>>
class set {
  template <class K, class M, class KC, class KH, class KE, class MH, class ME>
  friend class map;

  typedef internal::set_traits<T, Compare, Hash, Equal> traits;
  typedef internal::env<traits> env_type;
  typedef typename internal::node<traits> node_type;

  friend struct confluent::iterator<traits>;

 public:
  typedef T key_type;
  typedef T value_type;
  typedef set_provider<T, Compare, Hash, Equal> provider_type;
  typedef std::shared_ptr<provider_type> provider_ptr;
  typedef confluent::iterator<traits> iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;

  /**
   * Creates a new set.
   *
   * @param provider set_provider to use for this set (optional)
   *
   * Complexity: Constant in time and memory.
   **/
  set(provider_ptr provider = provider_type::default_provider())
      : provider_(std::move(provider)) {}

  /**
   * Creates a new set from a range of elements.
   *
   * @param first range start
   * @param last range end
   * @param provider set_provider to use for this set (optional)
   *
   * Complexity: O(n log n) expected time on random input. O(n) on presorted
   * input. O(n) in memory.
   **/
  template <class InputIterator>
  set(InputIterator first,
      InputIterator last,
      provider_ptr provider = provider_type::default_provider())
      : provider_(std::move(provider)) {
    insert(first, last);
  }

  /**
   * Creates a new set from an initializer_list.
   *
   * @param ilist the list of elements to include in the created set
   * @param provider set_provider to use for this set (optional)
   *
   * Complexity: O(n log n) expected time on random input. O(n) on presorted
   * input. O(n) in memory.
   **/
  set(std::initializer_list<value_type> ilist,
      provider_ptr provider = provider_type::default_provider())
      : provider_(std::move(provider)) {
    insert(ilist);
  }

  /**
   * Creates a new set from a range in another set.
   *
   * The created set will use the same set_provider as the source set.
   *
   * @param first range start
   * @param last range end
   *
   * Complexity: O(log n) expected time and memory.
   **/
  set(iterator first, iterator last) : set(*first.container_) {
    retain(first, last);
  }

  /**
   * Creates a new set as a copy of another set.
   *
   * The created set will use the same set_provider as the source set.
   *
   * @param other other set
   *
   * Complexity: Constant in time and memory.
   **/
  set(const set& other) : provider_(other.provider_), node_(other.node_) {}

  /**
   * Creates a new set by moving content from another set.
   *
   * The created set will use the same set_provider as the source set.
   *
   * Result is undefined if the other set is used after content has been moved.
   *
   * @param other other set
   *
   * Complexity: Constant in time and memory.
   **/
  set(set&& other)
      : provider_(std::move(other.provider_)), node_(std::move(other.node_)) {}

  ~set() { clear(); }

  /**
   * Inserts an element into this set.
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
   * Inserts a range of elements into this set.
   *
   * New element are inserted if they are not contained before.
   *
   * @param first range start
   * @param last range end
   * @return the number of inserted elements
   *
   * Complexity: Same cost as first creating a set from the given range and
   *     then inserting the created set into this set.
   **/
  template <class InputIterator>
  size_t insert(InputIterator first, InputIterator last) {
    return internal::insert(env(), &node_, first, last);
  }

  /**
   * Inserts elements from an initializer_list into this set.
   *
   * New element are inserted if they are not contained before.
   *
   * @param ilist the list of elements to insert
   * @return the number of inserted elements
   *
   * Complexity: Same cost as first creating a set from the given
   *     initializer_list and then inserting the created set into this set.
   **/
  size_t insert(std::initializer_list<value_type> ilist) {
    return internal::insert(env(), &node_, ilist.begin(), ilist.end());
  }

  /**
   * Inserts elements in another set into this set.
   *
   * New element are inserted if they are not contained before.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param other other set to insert elements from
   * @return the number of inserted elements
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   **/
  size_t insert(const set& other) {
    check(other);
    return internal::add(env(), &node_, other.node_);
  }

  /**
   * Erases an element from this set.
   *
   * The given element is erased if contained in the set.
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
   * Erases a range of elements from this set.
   *
   * The given range must be a range in this set.
   *
   * @param first range start
   * @param last range end
   * @return the number of erased elements
   *
   * Complexity: O(log n) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t erase(iterator first, iterator last) {
    check(first, last);
    return internal::erase(env(), &node_, first.pos_, last.pos_);
  }

  /**
   * Erases elements in another set from this set.
   *
   * After the operation this set will contain the set difference, i.e. all
   * elements that were present in this set but not in the other set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param other other set to erase elements from
   * @return the number of erased elements
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t erase(const set& other) {
    check(other);
    return internal::diff(env(), &internal::rank<traits>, &node_, other.node_);
  }

  /**
   * Retains a range of elements.
   *
   * The given range must be a range in this set.
   *
   * After the operation this set will contain the elements in the range.
   *
   * @param first range start
   * @param last range end
   * @return the number of erased elements
   *
   * Complexity: O(log n) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t retain(iterator first, iterator last) {
    check(first, last);
    return internal::retain(env(), &node_, first.pos_, last.pos_);
  }

  /**
   * Retains elements that are contained in another set.
   *
   * After the operation this set will contain the set intersection, i.e. all
   * elements that were present in this set and also in the other set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param other other set whose elements should be retained
   * @return the number of erased elements
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  size_t retain(const set& other) {
    check(other);
    return internal::intersect(env(), &internal::rank<traits>, &node_,
                               other.node_);
  }

  /**
   * Erases all elements in this set.
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
   * Swaps the content of this set with the content of another set.
   *
   * Complexity: Constant in time and memory.
   */
  void swap(set& other) {
    provider_.swap(other.provider_);
    node_.swap(other.node_);
  }

  // TODO: ilist assignment

  /**
   * Replaces the content of this set with the content of another set.
   *
   * After the operation this set will use the same provider as the other set.
   *
   * Complexity: Constant in time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set& operator=(const set& other) {
    clear();
    provider_ = other.provider_;
    node_ = other.node_;
    return *this;
  }

  /**
   * Replaces the content of this set with the content of another set.
   *
   * After the operation this set will use the same provider as the other set
   * used before the operation.
   *
   * Result is undefined if the other set is used after content has been moved.
   *
   * Complexity: Constant in time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set& operator=(set&& other) {
    swap(other);
    return *this;
  }

  /**
   * Replaces the content of this set with elements from an initializer_list.
   *
   * @param ilist the list of elements to assign
   *
   * Complexity: O(n log n) expected time on random input. O(n) on presorted
   * input. O(n) in memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set& operator=(std::initializer_list<value_type> ilist) {
    internal::assign(env(), &node_, ilist.begin(), ilist.end());
    return *this;
  }

  /**
   * Returns the union of this set and another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param rhs other set to merge with this set
   * @return a set containing all elements in this set and in the other set
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   **/
  set operator|(const set& rhs) const {
    check(rhs);
    return set(provider_, set_union(env(), node_, rhs.node_));
  }

  /**
   * Replaces the content of this set with the union of this set and another
   *set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param rhs other set to merge with this set
   * @return a reference to this set after it has been updated
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   **/
  set& operator|=(const set& rhs) {
    check(rhs);
    insert(rhs);
    return *this;
  }

  /**
   * Returns the intersection of this set and another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param rhs other set to merge with this set
   * @return a set containing all elements in this set and in the other set
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set operator&(const set& rhs) const {
    check(rhs);
    return set(provider_, set_intersection(env(), &internal::rank<traits>,
                                           node_, rhs.node_));
  }

  /**
   * Replaces the content of this set with the intersection of this set and
   * another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param rhs other set to merge with this set
   * @return a reference to this set after it has been updated
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set& operator&=(const set& rhs) {
    check(rhs);
    retain(rhs);
    return *this;
  }

  /**
   * Returns the difference of this set and another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param rhs other set to merge with this set
   * @return a set containing all elements in this set and in the other set
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set operator-(const set& rhs) const {
    check(rhs);
    return set(provider_, set_difference(env(), &internal::rank<traits>, node_,
                                         rhs.node_));
  }

  /**
   * Replaces the content of this set with the difference of this set and
   * another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param rhs other set to merge with this set
   * @return a reference to this set after it has been updated
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set& operator-=(const set& rhs) {
    check(rhs);
    erase(rhs);
    return *this;
  }

  /**
   * Returns the symmetric difference of this set and another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param rhs other set to merge with this set
   * @return a set containing all elements in this set and in the other set
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set operator^(const set& rhs) const {
    check(rhs);
    return set(provider_, set_symmetric(env(), node_, rhs.node_));
  }

  /**
   * Replaces the content of this set with the symmetric difference of this
   * set and another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param rhs other set to merge with this set
   * @return a reference to this set after it has been updated
   *
   * Let n be the size of the larger set.
   * Let m be the size of the smaller set.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: This operation might cause destruction of unused nodes, but the cost
   * of destructing nodes is always covered by the cost of creating them.
   **/
  set& operator^=(const set& rhs) {
    check(rhs);
    symmetric(env(), &node_, rhs.node_);
    return *this;
  }

  /**
   * Returns an iterator to the beginning of this set.
   **/
  iterator begin() const { return iterator(this, 0); }

  /**
   * Returns an iterator to the end of this set.
   **/
  iterator end() const { return iterator(this, size()); }

  /**
   * Returns an iterator to the beginning of this set.
   **/
  iterator cbegin() const { return begin(); }

  /**
   * Returns an iterator to the end of this set.
   **/
  iterator cend() const { return end(); }

  /**
   * Returns a reverse iterator to the beginning of this set.
   **/
  reverse_iterator rbegin() const { return reverse_iterator(end()); }

  /**
   * Returns a reverse iterator to the end of this set.
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
   * Returns an iterator to the first element not less than a given key.
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
   * Returns an iterator to the first element greater than a given key.
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
   * Returns the number of elements matching a given key
   *
   * @param key key to search for
   * @returns 0 if an element was found, otherwise 1
   *
   * Complexity: O(log n) expected time.
   **/
  size_t count(const key_type& key) const {
    const node_type* p =
        internal::lower_bound(node_.get(), [&](const node_type& p) {
          return key_comp(p.key(), key);
        }).first;
    return p && key_eq(p->key(), key) ? 1 : 0;
  }

  /**
   * Tests if this set includes the elements in another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @param other set to test if its elements are included in this set
   * @return true if all elements are included, false otherwise
   *
   * Let n be the size of this set.
   * Let m be the size of the other.
   * Let d be the size of the difference between this set and the other set.
   *
   * Complexity: O(min(m * log(n/m), d * log(n/d))) expected time and memory.
   *
   * Note: The operation will return directly if m > n.
   **/
  bool includes(const set& other) const {
    check(other);
    return internal::includes(env(), &internal::rank<traits>, node_,
                              other.node_);
  }

  /**
   * Returns a shared pointer to the set_provider used by this set.
   **/
  const provider_ptr& provider() const { return provider_; }

  /**
   * Tests if this set is empty.
   *
   * @return true if this set contains no elements, false otherwise
   *
   * Complexity: Constant in time.
   **/
  bool empty() const { return !node_; }

  /**
   * Returns the number of elements in this set.
   *
   * Complexity: Constant in time.
   **/
  size_t size() const { return internal::size(node_); }

  /**
   * Returns the combined hash value of all elements in this set.
   *
   * Complexity: Constant in time.
   **/
  size_t hash() const { return internal::hash(node_); }

  /**
   * Tests if this set contains the same elements as another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @return: true if this set contains the same elements as the other set,
   *     false otherwise
   *
   * Complexity: Constant in time.
   **/
  bool operator==(const set& rhs) const {
    check(rhs);
    return node_ == rhs.node_;
  }

  /**
   * Tests if this set does not contain the same elements as another set.
   *
   * Result is undefined if not both sets are using the same set_provider.
   *
   * @return: true if this set does not contain the same elements as the other
   *     set, false otherwise
   *
   * Complexity: Constant in time.
   **/
  bool operator!=(const set& rhs) const { return node_ != rhs.node_; }

 private:
  typedef typename internal::node_ptr<traits> node_ptr;

  set(provider_ptr provider, node_ptr node)
      : provider_(std::move(provider)), node_(std::move(node)) {}

  void check(const set& other) const { assert(provider_ == other.provider_); }

  void check(const iterator& first, const iterator& last) const {
    assert(provider_ == first.container_->provider_);
    assert(provider_ == last.container_->provider_);
    assert(first.pos_ <= last.pos_);
    assert(last.pos_ <= size());
  }

  bool key_comp(const key_type& lhs, const key_type& rhs) const {
    return provider_->key_comp()(lhs, rhs);
  }

  bool key_eq(const key_type& lhs, const key_type& rhs) const {
    return provider_->key_eq()(lhs, rhs);
  }

  env_type env() const { return {provider_.get()}; }

  provider_ptr provider_;
  node_ptr node_;
};

/**
 * Swaps content of two sets.
 *
 * swap(x, y);
 *
 * is equivalent to
 *
 * x.swap(y);
 **/
template <class T, class Compare, class Hash, class Equal>
void swap(set<T, Compare, Hash, Equal>& x, set<T, Compare, Hash, Equal>& y) {
  x.swap(y);
}

/**
 * Returns the combined hash value of a set.
 *
 * hash(x);
 *
 * is equivalent to
 *
 * x.hash();
 **/
template <class T, class Compare, class Hash, class Equal>
size_t hash(const set<T, Compare, Hash, Equal>& x) {
  return x.hash();
}

}  // namespace confluent

#endif  // CONFLUENT_SET_H_INCLUDED
