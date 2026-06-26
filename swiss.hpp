#pragma once
#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <sys/types.h>
#include <utility>
#include <initializer_list>
#include <stdexcept>

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

    private:
        friend class SwissTable;
		SwissTable* table_ = nullptr;
        size_t   index_ = 0;

    public:
        Iterator() = default;
        // TODO: add your constructor

        // return the reference to the element the iterator points to
		kvpair_t& operator*() const noexcept {
			// TODO:
        }
        // return the pointer to the element the iterator points to
		kvpair_t* operator->() const noexcept {
			// TODO:
         }

        // ++it. increment the iterator and return the iterator AFTER incrementing
		constexpr Iterator &operator++() noexcept {
            // TODO:
		}

        // it++. increment the iterator and return the iterator BEFORE incrementing
		constexpr Iterator operator++(int) noexcept {
            // TODO:
		}
		

        // return true if both iterators point to the same element in the same table
        bool operator==(const Iterator& o) const noexcept {
            // TODO:
        }

    };
	  
public:
    // destructor: free the space you have allocated
    ~SwissTable() {
        // TODO:
	}

	// capacity

	// return true if the table is empty, false otherwise
	[[nodiscard]] bool empty() const noexcept {
		// TODO:
	}

	// return the number of elements
	[[nodiscard]] size_t size() const noexcept {
		// TODO:
	}

	// optional: return the capacity of the table, i.e. the maximum number of elements
	// the table can hold without any rehash.
    [[nodiscard]] size_t max_size() const noexcept {
		// TODO:
    }

	// iterator interfaces
	// return begin iterator
	Iterator begin() noexcept {
		// TODO:
	}
	

	// return end iterator (the iterator AFTER the last element)
	Iterator end() noexcept {
        // TODO:
	}

	// common STL interface

	// clear the table but do NOT free the space
	void clear() noexcept {
        // TODO:
	}

    // insert a value by rvalue reference
    // return a pair of an iterator and a bool
    // - if the key exists, return {iter, false} where `iter` is an
    //   iterator pointing to the existing key.
    // - otherwise, insert the value, return {iter, true} where
    //   `iter` is an iterator pointing to the inserted key
	std::pair<Iterator, bool> insert(kvpair_t &&value) {
        // TODO:
	}

	// insert a value by reference
	// same requirement as above
	std::pair<Iterator, bool> insert(const kvpair_t &value) {
        // TODO:
	}

	// insert many values from the iterators of other containers
	template <std::input_iterator InputIt>
	void insert(InputIt first, InputIt last) {
        // TODO:
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
        // TODO:
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
        // TODO:
	}

	// const version of the one above
	const T &at(const Key &key) const {
        // TODO:
    }

	// get a value by the key.
	// if the key does not exist, insert {key, T{}} and return
	// a reference to the newly inserted mapped value.
	// this is the magic behind d[k] = v for a non-existing k.
	T &operator[](const Key &key) {
        // TODO:
	}

	// return 1 if the key exists, 0 otherwise
	[[nodiscard]] size_t count(const Key &key) const {
        // TODO:
	}

	// if the key exists, return the iterator associated with the key.
	// otherwise, return end()
	Iterator find(const Key &key) {
        // TODO:
	}

	// const version of the one above
	Iterator find(const Key &key) const {
        // TODO:
    }

	// return true if the key exists, false otherwise
	[[nodiscard]] bool contains(const Key &key) const {
        // TODO:
	}

	// reserve enough space for roughly `count` elements according to
	// your chosen load factor. The exact number of slots can be larger.
	// Leaving this empty is allowed by the interface, but your table will
	// be slow if many elements are inserted without reserving space.
	void reserve(size_t count) {
        // TODO:
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
        // TODO:
    }

    // explicit SwissTable(const Allocator& alloc) : alloc_(alloc) {}

	// constructor. insert a range of elements conveniently.
	// if bucket_count is non-zero, use it to reserve space before insertion
	template <std::input_iterator InputIt>
	SwissTable(InputIt first, InputIt last, size_t bucket_count = 0,
	           const Hash &hash = Hash(), const KeyEqual &equal = KeyEqual()
	        //    const Allocator &alloc = Allocator()
	           ) {
        // TODO:
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
        // TODO:
    }

	// move constructor
	SwissTable(SwissTable &&other) {
		// TODO:
    }

	// copy assignment
    SwissTable& operator=(const SwissTable& other) {
		// TODO:
		return *this;
    }

	// move assignment
    SwissTable& operator=(SwissTable&& other) {
		// TODO:
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
    void swap(SwissTable& other) {}

};

} // namespace swiss
