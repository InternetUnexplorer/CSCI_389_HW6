#include "lru_evictor.hh"

void LruEvictor::touch_key(const key_type &key) {
    // Check whether the key already exists
    auto entry = map.find(key);
    if (entry != map.end()) {
        // Remove the entry from the queue
        queue.erase(entry->second);
    }
    // Add the entry to the queue
    queue.emplace_back(key);
    map[key] = std::prev(queue.end());
}

const key_type LruEvictor::evict() {
    if (queue.empty()) {
        return "";
    } else {
        auto key = queue.front();
        queue.pop_front();
        map.erase(key);
        return key;
    }
}
