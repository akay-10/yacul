#ifndef UTILS_MEMORY_ALLOCATOR_H
#define UTILS_MEMORY_ALLOCATOR_H

#include "arena.h"

namespace utils { namespace memory {

// STL-compatible allocator that draws from a shared Arena.
// Suitable for std::vector, std::string, std::unordered_map, etc.
//
// IMPORTANT: deallocate() is a no-op. Memory is reclaimed only when the arena 
// resets or is destroyed.
//
// Example:
//   mem::Arena arena;
//   using IntVec = std::vector<int, mem::ArenaAllocator<int>>;
//   IntVec v(mem::ArenaAllocator<int>{arena});
template<typename T>
class ArenaAllocator {
 public:
  using value_type = T;

  explicit ArenaAllocator(Arena& arena) : arena_(&arena) {}

  template<typename U>
  ArenaAllocator(const ArenaAllocator<U>& other) : arena_(other.arena_) {}

  T* allocate(std::size_t n) {
    return static_cast<T*>(arena_->AllocBytes(sizeof(T) * n, alignof(T)));
  }

  void deallocate(T*, std::size_t) {
    // No-op: arena owns the memory.
  }

  template<typename U>
  bool operator==(const ArenaAllocator<U>& o) const {
    return arena_ == o.arena_;
  }

  template<typename U>
  bool operator!=(const ArenaAllocator<U>& o) const {
    return arena_ != o.arena_;
  }

  template<typename U> friend class ArenaAllocator;

 private:
  Arena* arena_;
};

}} // namespace utils::memory

#endif // UTILS_MEMORY_ALLOCATOR_H
