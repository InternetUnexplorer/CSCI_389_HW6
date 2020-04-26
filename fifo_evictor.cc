#include "fifo_evictor.hh"

void FifoEvictor::touch_key(const key_type &key) {
    if (keys.emplace(key).second) {
        queue.push_back(key);
    }
}

const key_type FifoEvictor::evict() {
    if (queue.empty()) {
        return "";
    } else {
        auto key = queue.front();
        queue.pop_front();
        keys.erase(key);
        return key;
    }
}
