#ifndef LRU_EVICTOR_HH
#define LRU_EVICTOR_HH

#include "evictor.hh"

#include <list>
#include <unordered_map>

class LruEvictor : public Evictor {
  private:
    std::list<key_type> queue;
    std::unordered_map<key_type, decltype(queue)::iterator> map;

  public:
    void touch_key(const key_type &key) override;
    const key_type evict() override;
};

#endif // LRU_EVICTOR_HH
