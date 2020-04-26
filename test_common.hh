#ifndef TEST_COMMON_HH
#define TEST_COMMON_HH

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS

#include "doctest/doctest.hh"

#include <map>

// Entries used for testing
const std::map<std::string, std::string> ENTRIES = {
    {"foo", "up"},     {"bar", "down"},    {"baz", "strange"},
    {"quux", "charm"}, {"quuz", "bottom"}, {"xyzzy", "top"},
};

// Combined size of the entries' data
const auto ENTRIES_SIZE = 33;

// First and last entries
const auto &FIRST_ENTRY = *ENTRIES.begin();
const auto &LAST_ENTRY = *std::prev(ENTRIES.end());

#endif // TEST_COMMON_HH
