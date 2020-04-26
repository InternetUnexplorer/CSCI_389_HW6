#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS

#include "cache.hh"
#include "fifo_evictor.hh"
#include "test_common.hh"

////////////////////////////////////////////////
// Cache Unit Tests
////////////////////////////////////////////////

TEST_CASE("Cache::used_space() on empty cache returns 0") {
    REQUIRE_EQ(Cache{0}.space_used(), 0);
}

TEST_CASE("Cache::get() on empty cache returns nullptr") {
    Cache cache{0};

    // Assert that entries are not in the cache
    for (auto &entry : ENTRIES) {
        Cache::size_type size;
        REQUIRE_EQ(cache.get(entry.first, size), nullptr);
    }
}

TEST_CASE("Cache::set() succeeds when cache has enough space") {
    Cache cache{ENTRIES_SIZE};
    auto space_used = 0;

    // Add entries to the cache
    for (auto &entry : ENTRIES) {
        cache.set(entry.first, entry.second.c_str(), entry.second.length() + 1);
        space_used += entry.second.length() + 1;
    }

    // Assert that space_used() is correct
    REQUIRE_EQ(cache.space_used(), space_used);

    // Assert that entries are in the cache
    for (auto &entry : ENTRIES) {
        // Get the entry from the cache
        Cache::size_type size = 0;
        auto value = cache.get(entry.first, size);
        // Assert that the entry was found
        REQUIRE_NE(value, nullptr);
        // Assert that the value and size are correct
        CHECK_EQ(entry.second, std::string(value));
        CHECK_EQ(entry.second.length() + 1, size);
    }
}

TEST_CASE("Cache::set() fails when cache has no evictor and lacks enough "
          "free space") {
    // Create a cache with enough space for all but the last entry
    const Cache::size_type MAXMEM =
        ENTRIES_SIZE - (LAST_ENTRY.second.length() + 1);
    Cache cache{MAXMEM};

    // Add entries to the cache
    for (auto &entry : ENTRIES) {
        cache.set(entry.first, entry.second.c_str(), entry.second.length() + 1);
    }

    // Assert that the last entry is not in the cache
    Cache::size_type size = 0;
    REQUIRE_EQ(cache.get(std::prev(ENTRIES.end())->first, size), nullptr);
    // Assert that `space_used()` is correct
    REQUIRE_EQ(cache.space_used(), MAXMEM);
}

TEST_CASE("Cache::set() fails when entry cannot possibly fit") {
    // Create a cache with less free space than the size of the first entry
    Cache cache{static_cast<Cache::size_type>(FIRST_ENTRY.first.length())};
    // Add the entry to the cache
    cache.set(FIRST_ENTRY.first, FIRST_ENTRY.second.c_str(),
              FIRST_ENTRY.second.length() + 1);
    // Assert that the entry is not in the cache
    Cache::size_type size = 0;
    REQUIRE_EQ(cache.get(FIRST_ENTRY.first, size), nullptr);
    // Assert that `space_used()` is still 0
    REQUIRE_EQ(cache.space_used(), 0);
}

TEST_CASE("Cache::set() replaces existing entry if present") {
    // Create a cache with enough space for all of the entries
    Cache cache{ENTRIES_SIZE};

    // Use the key of the first entry
    auto &key = FIRST_ENTRY.first;

    for (auto &entry : ENTRIES) {
        // Add or replace the entry
        cache.set(key, entry.second.c_str(), entry.second.length() + 1);
        // Get the entry from the cache
        Cache::size_type size = 0;
        auto value = cache.get(key, size);
        // Assert that the entry was found
        REQUIRE_NE(value, nullptr);
        // Assert that the value and size are correct
        CHECK_EQ(entry.second, std::string(value));
        CHECK_EQ(entry.second.length() + 1, size);
        // Assert that `space_used()` is correct
        REQUIRE_EQ(cache.space_used(), entry.second.length() + 1);
    }
}

TEST_CASE("Cache::del() succeeds when value is in cache") {
    Cache cache{ENTRIES_SIZE};
    auto space_used = 0;

    // Add entries to the cache
    for (auto &entry : ENTRIES) {
        cache.set(entry.first, entry.second.c_str(), entry.second.length() + 1);
        space_used += entry.second.length() + 1;
    }

    // Delete entries from the cache
    for (auto &entry : ENTRIES) {
        // Assert that the entry was removed from the cache
        CHECK(cache.del(entry.first));
        // Assert that `space_used()` decreased by the correct amount
        space_used -= entry.second.length() + 1;
        REQUIRE_EQ(cache.space_used(), space_used);
    }
}

TEST_CASE("Cache::del() fails when value is not in cache") {
    Cache cache{ENTRIES_SIZE};

    // Assert that all deletions fail
    for (auto &entry : ENTRIES) {
        CHECK(!cache.del(entry.first));
    }
    // Assert that `space_used()` is still 0
    REQUIRE_EQ(cache.space_used(), 0);
}

TEST_CASE("Cache::reset() removes all entries") {
    Cache cache{ENTRIES_SIZE};

    // Add entries to the cache
    for (auto &entry : ENTRIES) {
        cache.set(entry.first, entry.second.c_str(), entry.second.length() + 1);
    }

    // Remove all entries from the cache
    cache.reset();

    // Assert that entries are no longer in the cache
    for (auto &entry : ENTRIES) {
        Cache::size_type size;
        REQUIRE_EQ(cache.get(entry.first, size), nullptr);
    }
    // Assert that `space_used()` is 0
    REQUIRE_EQ(cache.space_used(), 0);
}

TEST_CASE("Cache::set() succeeds when cache has an evictor and entry can fit") {
    const Cache::size_type MAXMEM =
        ENTRIES_SIZE - (LAST_ENTRY.second.length() + 1);
    const auto MAX_LOAD_FACTOR = 0.75f;

    // Create a cache with enough space for all but the last entry
    FifoEvictor evictor;
    Cache cache{MAXMEM, MAX_LOAD_FACTOR, &evictor};

    // Add all but the last entry
    for (auto i = ENTRIES.begin(); i != std::prev(ENTRIES.end()); ++i) {
        cache.set(i->first, i->second.c_str(), i->second.length() + 1);
    }
    // Assert that cache is at capacity
    REQUIRE_EQ(cache.space_used(), MAXMEM);

    // Add the last entry
    cache.set(LAST_ENTRY.first, LAST_ENTRY.second.c_str(),
              LAST_ENTRY.second.length() + 1);
    // Assert that the entry is in the cache
    Cache::size_type size = 0;
    REQUIRE_NE(cache.get(LAST_ENTRY.first, size), nullptr);
    // Assert that cache is at or below capacity (eviction occurred)
    REQUIRE_LE(cache.space_used(), MAXMEM);
}
