#include <atomic>

class SpinLock {
  std::atomic_flag lck = ATOMIC_FLAG_INIT;
public:
  void lock() { while (lck.test_and_set(std::memory_order_acquire)); }
  void unlock() { lck.clear(std::memory_order_release); }
};


// Sorry, not the right place for this code. Maybe rename to common.hpp?
template<typename T>
inline void remove_every_second_item(T& c) {
  auto it = c.begin();
  while (it != c.end()){
    ++it;
    if (it != c.end()) {
      it = c.erase(it);
    }
  }
}
