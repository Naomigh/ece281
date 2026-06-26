#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace swiss {
// feel free to adjust the params for best performance
// naming convention: k stands for constant
static constexpr size_t kGroupSlots = 16;
static constexpr size_t kMaxAvgGroupLoad = 15;
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
        size_t pos_ = 0;

    public:
        Iterator() = default;
        Iterator(SwissTable* table, size_t index, size_t pos) noexcept
            : table_(table), index_(index), pos_(pos) {}

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
            if (table_ == nullptr || index_ >= table_->capacity_) {
                return *this;
            }
            if (pos_ != table_->kNpos) {
                ++pos_;
                while (pos_ < table_->order_.size()) {
                    const size_t candidate = table_->order_[pos_];
                    if (table_->is_live_index(candidate)) {
                        index_ = candidate;
                        return *this;
                    }
                    ++pos_;
                }
                index_ = table_->capacity_;
                return *this;
            }
            index_ = table_->next_live_slot(index_ + 1U);
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
    static constexpr std::uint64_t kMsbs = 0x8080808080808080ULL;
    static constexpr std::uint64_t kLsbs = 0x0101010101010101ULL;
    static constexpr size_t kNpos = static_cast<size_t>(-1);

    using storage_t = std::aligned_storage_t<sizeof(kvpair_t), alignof(kvpair_t)>;

    storage_t* slots_ = nullptr;
    unsigned char* ctrl_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
    size_t deleted_ = 0;
    std::vector<std::uint32_t> order_{};
    Hash hash_{};
    KeyEqual equal_{};

    static constexpr bool is_occupied(unsigned char ctrl) noexcept {
        return (ctrl & kDeleted) == 0;
    }

    [[nodiscard]] bool is_live_index(size_t index) const noexcept {
        return index < capacity_ && is_occupied(ctrl_[index]);
    }

    static constexpr size_t mix_hash(size_t hash) noexcept {
        if constexpr (sizeof(size_t) >= sizeof(std::uint64_t)) {
            return hash * static_cast<size_t>(0x9e3779b97f4a7c15ULL);
        } else {
            return hash * static_cast<size_t>(0x9e3779b9U);
        }
    }

    static constexpr size_t h1(size_t hash) noexcept {
        return hash;
    }

    static constexpr unsigned char h2(size_t hash) noexcept {
        if constexpr (sizeof(size_t) >= sizeof(std::uint64_t)) {
            return static_cast<unsigned char>((hash >> 57U) & kH2Mask);
        } else {
            return static_cast<unsigned char>((hash >> 25U) & kH2Mask);
        }
    }

    static constexpr std::uint64_t repeat_byte(unsigned char byte) noexcept {
        return kLsbs * static_cast<std::uint64_t>(byte);
    }

    static constexpr std::uint64_t match_byte_word(std::uint64_t word,
                                                   unsigned char byte) noexcept {
        const std::uint64_t x = word ^ repeat_byte(byte);
        return (x - kLsbs) & ~x & kMsbs;
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

    [[nodiscard]] std::uint64_t ctrl_word(size_t index) const noexcept {
        std::uint64_t word = 0;
#if defined(__GNUC__) || defined(__clang__)
        __builtin_memcpy(&word, ctrl_ + index, sizeof(word));
#else
        for (size_t i = 0; i < sizeof(word); ++i) {
            word |= static_cast<std::uint64_t>(ctrl_[index + i]) << (i * 8U);
        }
#endif
        return word;
    }

    static size_t slots_for_count(size_t count) {
        size_t needed = (count * kGroupSlots + kMaxAvgGroupLoad - 1)
                        / kMaxAvgGroupLoad;
        needed = std::max(needed, kGroupSlots);
        size_t capacity = kGroupSlots;
        while (capacity < needed) {
            capacity <<= 1U;
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
        ctrl_ = new unsigned char[capacity + kGroupSlots];
        std::fill(ctrl_, ctrl_ + capacity + kGroupSlots, kEmpty);
        capacity_ = capacity;
        size_ = 0;
        deleted_ = 0;
        order_.clear();
    }

    void destroy_elements() noexcept {
        for (std::uint32_t raw_index : order_) {
            const size_t index = raw_index;
            if (is_live_index(index)) {
                slot(index)->~kvpair_t();
            }
        }
        order_.clear();
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

    LookupResult lookup_with_hash(const Key& key, size_t hash,
                                  unsigned char tag) const {
        if (capacity_ == 0) {
            return {0, false};
        }

        const size_t mask = group_count() - 1U;
        size_t group = h1(hash) & mask;

        for (size_t step = 1; step <= group_count(); ++step) {
            const size_t begin = group * kGroupSlots;
            const std::uint64_t low = ctrl_word(begin);
            const std::uint64_t high = ctrl_word(begin + 8U);
            std::uint64_t matches = match_byte_word(low, tag);

            while (matches != 0) {
                const size_t offset = static_cast<size_t>(std::countr_zero(matches) >> 3U);
                const size_t index = begin + offset;
                if (equal_(slot(index)->first, key)) {
                    return {index, true};
                }
                matches &= matches - 1U;
            }

            matches = match_byte_word(high, tag);
            while (matches != 0) {
                const size_t offset = 8U + static_cast<size_t>(std::countr_zero(matches) >> 3U);
                const size_t index = begin + offset;
                if (equal_(slot(index)->first, key)) {
                    return {index, true};
                }
                matches &= matches - 1U;
            }

            if (match_byte_word(low, kEmpty) != 0
                || match_byte_word(high, kEmpty) != 0) {
                return {0, false};
            }
            group = (group + step) & mask;
        }
        return {0, false};
    }

    LookupResult lookup(const Key& key) const {
        const size_t hash = hash_(key);
        return lookup_with_hash(key, hash, h2(mix_hash(hash)));
    }

    LookupResult find_insert_position(const Key& key, size_t hash,
                                      unsigned char tag) {
        const size_t mask = group_count() - 1U;
        size_t group = h1(hash) & mask;
        for (size_t step = 1; step <= group_count(); ++step) {
            const size_t begin = group * kGroupSlots;
            const std::uint64_t low = ctrl_word(begin);
            const std::uint64_t high = ctrl_word(begin + 8U);
            std::uint64_t matches = match_byte_word(low, tag);

            while (matches != 0) {
                const size_t offset = static_cast<size_t>(std::countr_zero(matches) >> 3U);
                const size_t index = begin + offset;
                if (equal_(slot(index)->first, key)) {
                    return {index, true};
                }
                matches &= matches - 1U;
            }

            matches = match_byte_word(high, tag);
            while (matches != 0) {
                const size_t offset = 8U + static_cast<size_t>(std::countr_zero(matches) >> 3U);
                const size_t index = begin + offset;
                if (equal_(slot(index)->first, key)) {
                    return {index, true};
                }
                matches &= matches - 1U;
            }

            std::uint64_t available = match_byte_word(low, kEmpty);
            if (available != 0) {
                const size_t offset = static_cast<size_t>(std::countr_zero(available) >> 3U);
                return {begin + offset, false};
            }

            available = match_byte_word(high, kEmpty);
            if (available != 0) {
                const size_t offset = 8U + static_cast<size_t>(std::countr_zero(available) >> 3U);
                return {begin + offset, false};
            }
            group = (group + step) & mask;
        }
        return {capacity_, false};
    }

    void add_order(size_t index) {
        order_.push_back(static_cast<std::uint32_t>(index));
    }

    void compact_order_if_needed() {
        if (order_.size() <= size_ * 2U + 64U) {
            return;
        }
        std::vector<std::uint32_t> compact;
        compact.reserve(size_);
        for (std::uint32_t raw_index : order_) {
            if (is_live_index(raw_index)) {
                compact.push_back(raw_index);
            }
        }
        order_.swap(compact);
    }

    [[nodiscard]] size_t next_live_slot(size_t start) const noexcept {
        while (start < capacity_ && !is_occupied(ctrl_[start])) {
            ++start;
        }
        return start;
    }

    [[nodiscard]] Iterator iterator_at(size_t index) noexcept {
        return Iterator(this, index, kNpos);
    }

    template <class Pair>
    std::pair<Iterator, bool> insert_impl(Pair&& value) {
        const size_t hash = hash_(value.first);
        const unsigned char tag = h2(mix_hash(hash));
        if (should_grow(size_ + 1U)) {
            reserve(size_ + 1U);
        }

        LookupResult result = find_insert_position(value.first, hash, tag);
        if (result.found) {
            return {iterator_at(result.index), false};
        }
        if (result.index == capacity_) {
            rehash_to(std::max(capacity_ * 2U, slots_for_count(size_ + 1U)));
            result = find_insert_position(value.first, hash, tag);
        }

        const size_t index = result.index;
        if (ctrl_[index] == kDeleted) {
            --deleted_;
        }
        new (&slots_[index]) kvpair_t(std::forward<Pair>(value));
        ctrl_[index] = tag;
        ++size_;
        add_order(index);
        return {iterator_at(index), true};
    }

    void unchecked_insert_move(kvpair_t&& value) {
        const size_t hash = hash_(value.first);
        const unsigned char tag = h2(mix_hash(hash));
        LookupResult result = find_insert_position(value.first, hash, tag);
        if (result.index == capacity_) {
            rehash_to(std::max(capacity_ * 2U, slots_for_count(size_ + 1U)));
            result = find_insert_position(value.first, hash, tag);
        }
        const size_t index = result.index;
        new (&slots_[index]) kvpair_t(std::move(value));
        ctrl_[index] = tag;
        ++size_;
        add_order(index);
    }

    void rehash_to(size_t new_capacity) {
        SwissTable tmp;
        tmp.hash_ = hash_;
        tmp.equal_ = equal_;
        tmp.allocate_arrays(new_capacity);
        tmp.order_.reserve(size_);

        for (std::uint32_t raw_index : order_) {
            const size_t index = raw_index;
            if (is_live_index(index)) {
                tmp.unchecked_insert_move(std::move(*slot(index)));
            }
        }
        swap(tmp);
    }

    Iterator erase_index(size_t index, size_t pos_hint) {
        const size_t group_begin = (index / kGroupSlots) * kGroupSlots;
        (void)group_begin;
        slot(index)->~kvpair_t();
        ctrl_[index] = kDeleted;
        ++deleted_;
        --size_;

        Iterator next;
        if (pos_hint != kNpos) {
            size_t pos = pos_hint + 1U;
            while (pos < order_.size()) {
                const size_t candidate = order_[pos];
                if (is_live_index(candidate)) {
                    next = Iterator(this, candidate, kNpos);
                    break;
                }
                ++pos;
            }
            if (next.table_ == nullptr) {
                next = end();
            }
        } else {
            next = Iterator(this, next_live_slot(index + 1U), kNpos);
        }
        compact_order_if_needed();
        return next;
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
        size_t pos = 0;
        while (pos < order_.size()) {
            const size_t index = order_[pos];
            if (is_live_index(index)) {
                return Iterator(this, index, pos);
            }
            ++pos;
        }
        return end();
    }

    // return end iterator (the iterator AFTER the last element)
    Iterator end() noexcept {
        return Iterator(this, capacity_, order_.size());
    }

    // common STL interface

    // clear the table but do NOT free the space
    void clear() noexcept {
        destroy_elements();
        if (ctrl_ != nullptr) {
            std::fill(ctrl_, ctrl_ + capacity_ + kGroupSlots, kEmpty);
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
        const size_t hash = hash_(key);
        const unsigned char tag = h2(mix_hash(hash));
        if (should_grow(size_ + 1U)) {
            reserve(size_ + 1U);
        }

        LookupResult result = find_insert_position(key, hash, tag);
        if (result.found) {
            return {iterator_at(result.index), false};
        }
        if (result.index == capacity_) {
            rehash_to(std::max(capacity_ * 2U, slots_for_count(size_ + 1U)));
            result = find_insert_position(key, hash, tag);
        }

        const size_t index = result.index;
        if (ctrl_[index] == kDeleted) {
            --deleted_;
        }
        new (&slots_[index]) kvpair_t(std::piecewise_construct,
                                      std::forward_as_tuple(std::forward<K>(key)),
                                      std::forward_as_tuple(std::forward<Args>(args)...));
        ctrl_[index] = tag;
        ++size_;
        add_order(index);
        return {iterator_at(index), true};
    }

    // erase a value by its iterator
    // return the iterator AFTER the erased key
    Iterator erase(Iterator posIt) {
        if (posIt.table_ != this || posIt.index_ >= capacity_
            || !is_occupied(ctrl_[posIt.index_])) {
            return end();
        }
        return erase_index(posIt.index_, posIt.pos_);
    }

    // erase a value by its key
    size_t erase(const Key &key) {
        auto result = lookup(key);
        if (!result.found) {
            return 0;
        }
        (void)erase_index(result.index, kNpos);
        return 1;
    }

    // get a value by the key
    // if the key does not exist, throw std::out_of_range
    T &at(const Key &key) {
        auto result = lookup(key);
        if (!result.found) {
            throw std::out_of_range("SwissTable::at");
        }
        return slot(result.index)->second;
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
        return result.found ? iterator_at(result.index) : end();
    }

    // const version of the one above
    Iterator find(const Key &key) const {
        auto result = lookup(key);
        return result.found
                   ? Iterator(const_cast<SwissTable*>(this), result.index, kNpos)
                   : Iterator(const_cast<SwissTable*>(this), capacity_, order_.size());
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
        if (wanted > capacity_ || (deleted_ > size_ && wanted >= slots_for_count(size_))) {
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
        for (std::uint32_t raw_index : other.order_) {
            const size_t index = raw_index;
            if (other.is_live_index(index)) {
                insert(*other.slot(index));
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
        swap(order_, other.order_);
        swap(hash_, other.hash_);
        swap(equal_, other.equal_);
    }
};

} // namespace swiss
