#include "cache.hh"

#include <numeric>
#include <unordered_map>
#include <vector>

class Cache::Impl {
  private:
    const size_type maxmem;
    Evictor *const evictor;

    size_type usedmem = 0;

    using data_type = std::vector<byte_type>;
    using map_type = std::unordered_map<key_type, data_type, hash_func>;

    map_type entries;

  public:
    Impl(size_type maxmem, float max_load_factor, Evictor *evictor,
         const hash_func &hasher)
    : maxmem{maxmem}, evictor{evictor}, entries{map_type{0, hasher}} {
        entries.max_load_factor(max_load_factor);
    }

    void set(const key_type &key, val_type val, size_type size) {
        // Delete the existing entry if present
        del(key);
        // Evict if necessary to make space for the entry
        while (space_used() + size > maxmem) {
            // Return if there is no evictor or if the entry cannot possibly fit
            // in the cache even with eviction
            if (evictor == nullptr || size > maxmem) {
                return;
            }
            // Get entry to evict from evictor
            auto entry_key = evictor->evict();
            // Return if evictor does not know what to evict
            if (entry_key == "") {
                return;
            }
            // Evict the entry
            del(entry_key);
        }
        // Copy the data, add the entry, and update `usedmem`
        usedmem += (entries[key] = data_type{val, val + size}).size();
        // If there is an evictor, inform it that the key has been touched
        if (evictor != nullptr) {
            evictor->touch_key(key);
        }
    }

    val_type get(const key_type &key, size_type &val_size) const {
        // Search for an entry matching the key
        auto entry = entries.find(key);
        // Check whether an entry was found
        if (entry != entries.end()) {
            // If there is an evictor, inform it that the key has been touched
            if (evictor != nullptr) {
                evictor->touch_key(key);
            }
            // Set the size and return the data
            val_size = entry->second.size();
            return entry->second.data();
        } else {
            // Return nullptr if entry does not exist
            return nullptr;
        }
    }

    bool del(const key_type &key) {
        // Search for an entry matching the key
        auto entry = entries.find(key);
        // Check whether an entry was found
        if (entry != entries.end()) {
            // Update `usedmem`
            usedmem -= entry->second.size();
            // Delete the entry
            entries.erase(entry);
            // Return true (success)
            return true;
        } else {
            // Return true (failure)
            return false;
        }
    }

    size_type space_used() const {
        return usedmem;
    }

    void reset() {
        // Remove all entries
        entries.clear();
        // Reset `usedmem`
        usedmem = 0;
    }
};

Cache::Cache(size_type maxmem, float max_load_factor, Evictor *evictor,
             hash_func hasher)
: pImpl_(new Impl{maxmem, max_load_factor, evictor, hasher}) {}

Cache::~Cache() = default;

void Cache::set(key_type key, val_type val, size_type size) {
    pImpl_->set(key, val, size);
}

Cache::val_type Cache::get(key_type key, size_type &val_size) const {
    return pImpl_->get(key, val_size);
}

bool Cache::del(key_type key) {
    return pImpl_->del(key);
}

Cache::size_type Cache::space_used() const {
    return pImpl_->space_used();
}

void Cache::reset() {
    pImpl_->reset();
}
