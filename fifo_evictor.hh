#ifndef FIFO_EVICTOR_HH
#define FIFO_EVICTOR_HH

#include "evictor.hh"

#include <list>
#include <unordered_set>

class FifoEvictor : public Evictor {
  private:
    std::list<key_type> queue;
    std::unordered_set<key_type> keys;

  public:
    void touch_key(const key_type &key) override;
    const key_type evict() override;
};

#endif // FIFO_EVICTOR_HH
