#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS

#include "cache.hh"
#include "test_common.hh"

#include <boost/process.hpp>
#include <functional>
#include <iostream>
#include <string>

/// Server address and port
constexpr auto SERVER_ADDRESS = "localhost";
constexpr auto SERVER_PORT = "4022";

/// Returns a new client
Cache make_client() {
    return Cache{SERVER_ADDRESS, SERVER_PORT};
}

/// Spawn the server as a child process and run the provided function after it
/// has started
void run_with_server(const Cache::size_type maxmem,
                     const std::function<void()> &inner) {
    /// Spawn the server as a child process, capturing stdout
    boost::process::ipstream std_out;
    boost::process::child server(
        "./cache_server", "--server", SERVER_ADDRESS, "--port", SERVER_PORT,
        "--maxmem", std::to_string(maxmem), boost::process::std_out > std_out);
    // Wait for the line that says the server is running
    std::string line;
    std::getline(std_out, line);
    // Run the provided function
    inner();
    // Terminate the server process (a bit unclean, but there doesn't seem to be
    // a nice platform-independent way to send SIGINT)
    server.terminate();
}

////////////////////////////////////////////////
// Cache Client Unit Tests
////////////////////////////////////////////////

TEST_CASE("Cache::used_space() on empty cache returns 0") {
    run_with_server(0, [&] { REQUIRE_EQ(make_client().space_used(), 0); });
}

TEST_CASE("Cache::get() on empty cache returns nullptr") {
    run_with_server(0, [&] {
        auto cache = make_client();

        // Assert that entries are not in the cache
        for (auto &entry : ENTRIES) {
            Cache::size_type size;
            REQUIRE_EQ(cache.get(entry.first, size), nullptr);
        }
    });
}

TEST_CASE("Cache::set() succeeds when cache has enough space") {
    run_with_server(ENTRIES_SIZE, [&] {
        auto cache = make_client();
        auto space_used = 0;

        // Add entries to the cache
        for (auto &entry : ENTRIES) {
            cache.set(entry.first, entry.second.c_str(),
                      entry.second.length() + 1);
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
    });
}

TEST_CASE("Cache::set() fails when cache has no evictor and lacks enough "
          "free space") {
    // Create a cache with enough space for all but the last entry
    const Cache::size_type MAXMEM =
        ENTRIES_SIZE - (LAST_ENTRY.second.length() + 1);
    run_with_server(MAXMEM, [&] {
        auto cache = make_client();

        // Add entries to the cache
        for (auto &entry : ENTRIES) {
            cache.set(entry.first, entry.second.c_str(),
                      entry.second.length() + 1);
        }

        // Assert that the last entry is not in the cache
        Cache::size_type size = 0;
        REQUIRE_EQ(cache.get(std::prev(ENTRIES.end())->first, size), nullptr);
        // Assert that `space_used()` is correct
        REQUIRE_EQ(cache.space_used(), MAXMEM);
    });
}

TEST_CASE("Cache::set() fails when entry cannot possibly fit") {
    // Create a cache with less free space than the size of the first entry
    const auto MAXMEM =
        static_cast<Cache::size_type>(FIRST_ENTRY.first.length());
    run_with_server(MAXMEM, [&] {
        auto cache = make_client();

        // Add the entry to the cache
        cache.set(FIRST_ENTRY.first, FIRST_ENTRY.second.c_str(),
                  FIRST_ENTRY.second.length() + 1);
        // Assert that the entry is not in the cache
        Cache::size_type size = 0;
        REQUIRE_EQ(cache.get(FIRST_ENTRY.first, size), nullptr);
        // Assert that `space_used()` is still 0
        REQUIRE_EQ(cache.space_used(), 0);
    });
}

TEST_CASE("Cache::set() replaces existing entry if present") {
    // Create a cache with enough space for all of the entries
    run_with_server(ENTRIES_SIZE, [&] {
        auto cache = make_client();

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
    });
}

TEST_CASE("Cache::del() succeeds when value is in cache") {
    run_with_server(ENTRIES_SIZE, [&] {
        auto cache = make_client();
        auto space_used = 0;

        // Add entries to the cache
        for (auto &entry : ENTRIES) {
            cache.set(entry.first, entry.second.c_str(),
                      entry.second.length() + 1);
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
    });
}

TEST_CASE("Cache::del() fails when value is not in cache") {
    run_with_server(ENTRIES_SIZE, [&] {
        auto cache = make_client();

        // Assert that all deletions fail
        for (auto &entry : ENTRIES) {
            CHECK(!cache.del(entry.first));
        }
        // Assert that `space_used()` is still 0
        REQUIRE_EQ(cache.space_used(), 0);
    });
}

TEST_CASE("Cache::reset() removes all entries") {
    run_with_server(ENTRIES_SIZE, [&] {
        auto cache = make_client();

        // Add entries to the cache
        for (auto &entry : ENTRIES) {
            cache.set(entry.first, entry.second.c_str(),
                      entry.second.length() + 1);
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
    });
}
