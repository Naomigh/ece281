#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <sys/types.h>
#include <type_traits>
#include <utility>

namespace swiss {
// feel free to adjust the params for best performance
// naming convention: k stands for constant
static constexpr size_t  kGroupSlots   = 8;
static constexpr size_t  kMaxAvgGroupLoad = 7;
// load factor = maxAvgGroupLoad / maxGroupSlots


template <class Key, class T, class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>
        //   optional: uncomment this line if you want to support custom allocator
        //   class Allocator = std::allocator<std::pair<const Key, T>>
          >
class SwissTable {
public:
    using kvpair_t = std::pair<const Key, T>;

private:
public:
    // invariant: an Iterator ALWAYS points to a valid item directly (except end())
    class Iterator {
    public:
        // define the category of the iterator. you can ignore this line
        using iterator_category = std::forward_iterator_tag;
        using value_type = kvpair_t;
        using difference_type = std::ptrdiff_t;
        using pointer = kvpair_t*;
        using reference = kvpair_t&;

    private:
        friend class SwissTable;
        SwissTable* table_ = nullptr;
        size_t index_ = 0;

    public:
        Iterator() = default;
        Iterator(SwissTable* table, size_t index) noexcept
            : table_(table), index_(index) {}

        // return the reference to the element the iterator points to
        kvpair_t& operator*() const noexcept {
            return *table_->slot(index_);
        }

        // return the pointer to the element the iterator points to
        kvpair_t* operator->() const noexcept {
            return table_->slot(index_);
        }

        // ++it. increment the iterator and return the iterator AFTER incrementing
        constexpr Iterator &operator++() noexcept {
            if (table_ != nullptr && index_ < table_->capacity_) {
                ++index_;
                while (index_ < table_->capacity_
                       && !table_->is_occupied(table_->ctrl_[index_])) {
                    ++index_;
                }
            }
            return *this;
        }

        // it++. increment the iterator and return the iterator BEFORE incrementing
        constexpr Iterator operator++(int) noexcept {
            Iterator old = *this;
            ++(*this);
            return old;
        }

        // return true if both iterators point to the same element in the same table
        bool operator==(const Iterator& o) const noexcept {
            return table_ == o.table_ && index_ == o.index_;
        }

        bool operator!=(const Iterator& o) const noexcept {
            return !(*this == o);
        }
    };

private:
    static constexpr unsigned char kEmpty = 0xfe;
    static constexpr unsigned char kDeleted = 0x80;
    static constexpr unsigned char kH2Mask = 0x7f;

    using storage_t = std::aligned_storage_t<sizeof(kvpair_t), alignof(kvpair_t)>;

    storage_t* slots_ = nullptr;
    unsigned char* ctrl_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
    size_t deleted_ = 0;
    Hash hash_{};
    KeyEqual equal_{};

    static constexpr bool is_occupied(unsigned char ctrl) noexcept {
        return (ctrl & kDeleted) == 0;
    }

    static constexpr size_t mix_hash(size_t hash) noexcept {
        if constexpr (sizeof(size_t) >= sizeof(std::uint64_t)) {
            hash ^= hash >> 33U;
            hash *= static_cast<size_t>(0xff51afd7ed558ccdULL);
            hash ^= hash >> 33U;
            hash *= static_cast<size_t>(0xc4ceb9fe1a85ec53ULL);
            hash ^= hash >> 33U;
        } else {
            hash ^= hash >> 16U;
            hash *= static_cast<size_t>(0x7feb352dU);
            hash ^= hash >> 15U;
            hash *= static_cast<size_t>(0x846ca68bU);
            hash ^= hash >> 16U;
        }
        return hash;
    }

    static constexpr size_t h1(size_t hash) noexcept {
        return hash >> 7U;
    }

    static constexpr unsigned char h2(size_t hash) noexcept {
        return static_cast<unsigned char>(hash & kH2Mask);
    }

    [[nodiscard]] kvpair_t* slot(size_t index) noexcept {
        return std::launder(reinterpret_cast<kvpair_t*>(&slots_[index]));
    }

    [[nodiscard]] const kvpair_t* slot(size_t index) const noexcept {
        return std::launder(reinterpret_cast<const kvpair_t*>(&slots_[index]));
    }

    [[nodiscard]] size_t group_count() const noexcept {
        return capacity_ / kGroupSlots;
    }

    static size_t slots_for_count(size_t count) {
        size_t needed = (count * kGroupSlots + kMaxAvgGroupLoad - 1)
                        / kMaxAvgGroupLoad;
        needed = std::max(needed, kGroupSlots);
        size_t capacity = kGroupSlots;
        while (capacity < needed) {
            capacity *= 2;
        }
        return capacity;
    }

    [[nodiscard]] bool should_grow(size_t wanted_size) const noexcept {
        return capacity_ == 0
               || wanted_size * kGroupSlots > capacity_ * kMaxAvgGroupLoad
               || (wanted_size + deleted_) * kGroupSlots
                      > capacity_ * kMaxAvgGroupLoad;
    }

    void allocate_arrays(size_t capacity) {
        slots_ = static_cast<storage_t*>(
            ::operator new[](capacity * sizeof(storage_t),
                             std::align_val_t{alignof(storage_t)}));
        ctrl_ = new unsigned char[capacity];
        std::fill(ctrl_, ctrl_ + capacity, kEmpty);
        capacity_ = capacity;
        size_ = 0;
        deleted_ = 0;
    }

    void destroy_elements() noexcept {
        for (size_t i = 0; i < capacity_; ++i) {
            if (is_occupied(ctrl_[i])) {
                slot(i)->~kvpair_t();
            }
        }
        size_ = 0;
        deleted_ = 0;
    }

    void free_arrays() noexcept {
        if (slots_ != nullptr) {
            destroy_elements();
            ::operator delete[](slots_, std::align_val_t{alignof(storage_t)});
            delete[] ctrl_;
        }
        slots_ = nullptr;
        ctrl_ = nullptr;
        capacity_ = 0;
    }

    struct LookupResult {
        size_t index = 0;
        bool found = false;
    };

    LookupResult lookup(const Key& key) const {
        if (capacity_ == 0) {
            return {0, false};
        }

        const size_t hash = mix_hash(hash_(key));
        const unsigned char tag = h2(hash);
        const size_t groups = group_count();
        const size_t start = h1(hash) & (groups - 1);

        for (size_t probe = 0; probe < groups; ++probe) {
            const size_t group = (start + (probe * probe + probe) / 2) & (groups - 1);
            const size_t begin = group * kGroupSlots;
            bool has_empty = false;

            for (size_t offset = 0; offset < kGroupSlots; ++offset) {
                const size_t index = begin + offset;
                const unsigned char ctrl = ctrl_[index];
                if (ctrl == kEmpty) {
                    has_empty = true;
                } else if (ctrl == tag && equal_(slot(index)->first, key)) {
                    return {index, true};
                }
            }

            if (has_empty) {
                return {0, false};
            }
        }
        return {0, false};
    }

    LookupResult find_insert_position(const Key& key) {
        const size_t hash = mix_hash(hash_(key));
        const unsigned char tag = h2(hash);
        const size_t groups = group_count();
        const size_t start = h1(hash) & (groups - 1);
        size_t first_available = capacity_;

        for (size_t probe = 0; probe < groups; ++probe) {
            const size_t group = (start + (probe * probe + probe) / 2) & (groups - 1);
            const size_t begin = group * kGroupSlots;

            for (size_t offset = 0; offset < kGroupSlots; ++offset) {
                const size_t index = begin + offset;
                const unsigned char ctrl = ctrl_[index];
                if (ctrl == tag && equal_(slot(index)->first, key)) {
                    return {index, true};
                }
                if ((ctrl == kDeleted || ctrl == kEmpty)
                    && first_available == capacity_) {
                    first_available = index;
                }
            }

            for (size_t offset = 0; offset < kGroupSlots; ++offset) {
                if (ctrl_[begin + offset] == kEmpty) {
                    return {first_available, false};
                }
            }
        }
        return {first_available, false};
    }

    template <class Pair>
    std::pair<Iterator, bool> insert_impl(Pair&& value) {
        if (should_grow(size_ + 1)) {
            reserve(size_ + 1);
        }

        LookupResult result = find_insert_position(value.first);
        if (result.found) {
            return {Iterator(this, result.index), false};
        }

        const size_t index = result.index;
        if (ctrl_[index] == kDeleted) {
            --deleted_;
        }
        new (&slots_[index]) kvpair_t(std::forward<Pair>(value));
        ctrl_[index] = h2(mix_hash(hash_(slot(index)->first)));
        ++size_;
        return {Iterator(this, index), true};
    }

    void rehash_to(size_t new_capacity) {
        SwissTable tmp;
        tmp.hash_ = hash_;
        tmp.equal_ = equal_;
        tmp.allocate_arrays(new_capacity);

        for (size_t i = 0; i < capacity_; ++i) {
            if (is_occupied(ctrl_[i])) {
                tmp.insert(std::move(*slot(i)));
            }
        }
        swap(tmp);
    }

public:
    // destructor: free the space you have allocated
    ~SwissTable() {
        free_arrays();
    }

    // capacity

    // return true if the table is empty, false otherwise
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    // return the number of elements
    [[nodiscard]] size_t size() const noexcept {
        return size_;
    }

    // optional: return the capacity of the table, i.e. the maximum number of elements
    // the table can hold without any rehash.
    [[nodiscard]] size_t max_size() const noexcept {
        return capacity_ * kMaxAvgGroupLoad / kGroupSlots;
    }

    // iterator interfaces
    // return begin iterator
    Iterator begin() noexcept {
        size_t index = 0;
        while (index < capacity_ && !is_occupied(ctrl_[index])) {
            ++index;
        }
        return Iterator(this, index);
    }

    // return end iterator (the iterator AFTER the last element)
    Iterator end() noexcept {
        return Iterator(this, capacity_);
    }

    // common STL interface

    // clear the table but do NOT free the space
    void clear() noexcept {
        destroy_elements();
        if (ctrl_ != nullptr) {
            std::fill(ctrl_, ctrl_ + capacity_, kEmpty);
        }
    }

    // insert a value by rvalue reference
    // return a pair of an iterator and a bool
    // - if the key exists, return {iter, false} where `iter` is an
    //   iterator pointing to the existing key.
    // - otherwise, insert the value, return {iter, true} where
    //   `iter` is an iterator pointing to the inserted key
    std::pair<Iterator, bool> insert(kvpair_t &&value) {
        return insert_impl(std::move(value));
    }

    // insert a value by reference
    // same requirement as above
    std::pair<Iterator, bool> insert(const kvpair_t &value) {
        return insert_impl(value);
    }

    // insert many values from the iterators of other containers
    template <std::input_iterator InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    // KEEP THIS: this method is called in the test script, do not delete it!
    // a default implementation is provided
    // insert a value by the key AND arguments passed to its constructor
    template <class K, class... Args> std::pair<Iterator, bool> try_emplace(K&& key, Args &&...args) {
        // this function can be implemented differently
        // you are encouraged to explore a better implementation that has more cpp flavor
        return insert(kvpair_t(std::forward<K>(key), T(std::forward<Args>(args)...)));
    }

    // erase a value by its iterator
    // return the iterator AFTER the erased key
    Iterator erase(Iterator posIt) {
        if (posIt.table_ != this || posIt.index_ >= capacity_
            || !is_occupied(ctrl_[posIt.index_])) {
            return end();
        }

        const size_t index = posIt.index_;
        const size_t group_begin = (index / kGroupSlots) * kGroupSlots;
        bool group_has_empty = false;
        for (size_t offset = 0; offset < kGroupSlots; ++offset) {
            if (ctrl_[group_begin + offset] == kEmpty) {
                group_has_empty = true;
                break;
            }
        }

        slot(index)->~kvpair_t();
        ctrl_[index] = group_has_empty ? kEmpty : kDeleted;
        if (!group_has_empty) {
            ++deleted_;
        }
        --size_;

        Iterator next(this, index);
        ++next;
        return next;
    }

    // erase a value by its key
    size_t erase(const Key &key) {
        // you can implement your own logic
        auto it = find(key);
        if (it == end())
            return 0;
        erase(it);
        return 1;
    }

    // get a value by the key
    // if the key does not exist, throw std::out_of_range
    T &at(const Key &key) {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("SwissTable::at");
        }
        return it->second;
    }

    // const version of the one above
    const T &at(const Key &key) const {
        auto result = lookup(key);
        if (!result.found) {
            throw std::out_of_range("SwissTable::at");
        }
        return slot(result.index)->second;
    }

    // get a value by the key.
    // if the key does not exist, insert {key, T{}} and return
    // a reference to the newly inserted mapped value.
    // this is the magic behind d[k] = v for a non-existing k.
    T &operator[](const Key &key) {
        return try_emplace(key).first->second;
    }

    // return 1 if the key exists, 0 otherwise
    [[nodiscard]] size_t count(const Key &key) const {
        return contains(key) ? 1U : 0U;
    }

    // if the key exists, return the iterator associated with the key.
    // otherwise, return end()
    Iterator find(const Key &key) {
        auto result = lookup(key);
        return result.found ? Iterator(this, result.index) : end();
    }

    // const version of the one above
    Iterator find(const Key &key) const {
        auto result = lookup(key);
        return result.found
                   ? Iterator(const_cast<SwissTable*>(this), result.index)
                   : Iterator(const_cast<SwissTable*>(this), capacity_);
    }

    // return true if the key exists, false otherwise
    [[nodiscard]] bool contains(const Key &key) const {
        return lookup(key).found;
    }

    // reserve enough space for roughly `count` elements according to
    // your chosen load factor. The exact number of slots can be larger.
    // Leaving this empty is allowed by the interface, but your table will
    // be slow if many elements are inserted without reserving space.
    void reserve(size_t count) {
        const size_t wanted = slots_for_count(count);
        if (wanted > capacity_ || deleted_ > size_) {
            rehash_to(std::max(wanted, slots_for_count(size_)));
        }
    }

    // STL boilerplate: constructors, copying, etc.

    // implement it if you want
    // [[nodiscard]] allocator_type get_allocator() const noexcept { return ...; }
    // [[nodiscard]] hasher hash_function() const { return ...; }
    // [[nodiscard]] key_equal key_eq() const { return ...; }

    SwissTable() = default;

    // construct an empty table and reserve enough space for roughly `count` elements
    explicit SwissTable(size_t count, const Hash &hash = Hash(),
                        const KeyEqual &equal = KeyEqual()
                        // const Allocator &alloc = Allocator()
                        ) {
        hash_ = hash;
        equal_ = equal;
        reserve(count);
    }

    // explicit SwissTable(const Allocator& alloc) : alloc_(alloc) {}

    // constructor. insert a range of elements conveniently.
    // if bucket_count is non-zero, use it to reserve space before insertion
    template <std::input_iterator InputIt>
    SwissTable(InputIt first, InputIt last, size_t bucket_count = 0,
               const Hash &hash = Hash(), const KeyEqual &equal = KeyEqual()
            //    const Allocator &alloc = Allocator()
               ) {
        hash_ = hash;
        equal_ = equal;
        if (bucket_count != 0) reserve(bucket_count);
        for (; first != last; ++first) insert(*first);
    }

    // this constructor enables you to use initializer lists conveniently
    SwissTable(std::initializer_list<kvpair_t> init,
               size_t bucket_count = 0, const Hash &hash = Hash(),
               const KeyEqual &equal = KeyEqual()
            //    const Allocator &alloc = Allocator()
               )
        : SwissTable(init.begin(), init.end(), bucket_count, hash, equal) {} // add alloc if you want

    // copy constructor
    SwissTable(const SwissTable& other) {
        hash_ = other.hash_;
        equal_ = other.equal_;
        reserve(other.size_);
        for (size_t i = 0; i < other.capacity_; ++i) {
            if (is_occupied(other.ctrl_[i])) {
                insert(*other.slot(i));
            }
        }
    }

    // move constructor
    SwissTable(SwissTable &&other) {
        swap(other);
    }

    // copy assignment
    SwissTable& operator=(const SwissTable& other) {
        if (this != &other) {
            SwissTable tmp(other);
            swap(tmp);
        }
        return *this;
    }

    // move assignment
    SwissTable& operator=(SwissTable&& other) {
        if (this != &other) {
            clear();
            free_arrays();
            swap(other);
        }
        return *this;
    }

    // enable syntax like table = {{1,2},{3,4}};
    SwissTable& operator=(std::initializer_list<kvpair_t> ilist) {
        clear();
        reserve(ilist.size());
        for (auto& v : ilist) insert(v);
        return *this;
    }

    // optional: swap with another table.
    // conceptually equivalent to std::swap(*this, other)
    void swap(SwissTable& other) {
        using std::swap;
        swap(slots_, other.slots_);
        swap(ctrl_, other.ctrl_);
        swap(capacity_, other.capacity_);
        swap(size_, other.size_);
        swap(deleted_, other.deleted_);
        swap(hash_, other.hash_);
        swap(equal_, other.equal_);
    }
};

} // namespace swiss
