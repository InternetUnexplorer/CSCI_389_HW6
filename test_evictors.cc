#include "cache.hh"
#include "fifo_evictor.hh"
#include "lru_evictor.hh"
#include "test_common.hh"

////////////////////////////////////////////////
// FifoEvictor Unit Tests
////////////////////////////////////////////////

TEST_CASE("FifoEvictor::evict() returns \"\" when there are no keys") {
    REQUIRE_EQ(FifoEvictor{}.evict(), "");
}

TEST_CASE("FifoEvictor::evict() returns keys FIFO order") {
    FifoEvictor evictor;
    // Touch keys in forward order
    for (auto &entry : ENTRIES) {
        evictor.touch_key(entry.first);
    }
    // Touch keys in reverse order (should have no effect)
    for (auto i = ENTRIES.rbegin(); i != ENTRIES.rend(); ++i) {
        evictor.touch_key(i->first);
    }
    // Assert that keys are evicted in forward order
    for (auto &entry : ENTRIES) {
        REQUIRE_EQ(evictor.evict(), entry.first);
    }
}

////////////////////////////////////////////////
// LruEvictor Unit Tests
////////////////////////////////////////////////

TEST_CASE("LruEvictor::evict() returns \"\" when there are no keys") {
    REQUIRE_EQ(LruEvictor{}.evict(), "");
}

TEST_CASE("LruEvictor::evict() returns keys in LRU order") {
    LruEvictor evictor;
    // Touch keys in forward order
    for (auto &entry : ENTRIES) {
        evictor.touch_key(entry.first);
    }
    // Touch keys in reverse order
    for (auto i = ENTRIES.rbegin(); i != ENTRIES.rend(); ++i) {
        evictor.touch_key(i->first);
    }
    // Assert that keys are evicted in reverse order
    for (auto i = ENTRIES.rbegin(); i != ENTRIES.rend(); ++i) {
        REQUIRE_EQ(evictor.evict(), i->first);
    }
}
