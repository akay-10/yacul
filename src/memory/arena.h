#ifndef UTILS_MEMORY_ARENA_H
#define UTILS_MEMORY_ARENA_H

#include <memory> // shared_ptr

#include "basic/basic.h"
#include "logging/logger.h"

namespace utils { namespace memory {

namespace detail {

inline std::size_t AlignUp(std::size_t value, std::size_t align) {
  CHECK(IsPowerOfTwo(align)) << "Alignment must be power of two";
  return ::AlignUp(value, align);
}

inline void CheckAlignment(std::size_t align) {
  CHECK(align != 0 && IsPowerOfTwo(align) && align <= 4096U)
    << "Arena: alignment must be a non-zero power-of-two <= 4096";
}

struct AllocHeader { void* malloc_ptr; };

} // namespace detail

// A raw memory slab owned by the Arena. Blocks form a singly-linked list.
struct Block {
  // Next block (may be null).
  Block* next;
  // Usable bytes (excludes this header).
  std::size_t capacity;
  // Bytes consumed so far.
  std::size_t used;

  // Pointer to the first usable byte.
  std::byte* data() {
    return reinterpret_cast<std::byte*>(this) +
      detail::AlignUp(sizeof(Block), alignof(std::max_align_t));
  }

  const std::byte* data() const {
    return reinterpret_cast<const std::byte*>(this) +
      detail::AlignUp(sizeof(Block), alignof(std::max_align_t));
  }

  std::size_t remaining() const { return capacity - used; }

  void* TryAlloc(std::size_t size, std::size_t align) {
    std::byte* base = data() + used;
    std::size_t padding = detail::AlignUp(
      reinterpret_cast<std::uintptr_t>(base), align) -
      reinterpret_cast<std::uintptr_t>(base);
    std::size_t total = padding + size;
    if (total > remaining()) return nullptr;
    void* ptr = base + padding;
    used += total;
    return ptr;
  }
};

// Forward declaration.
class Arena;

// RAII rewind-point for temporary scratch allocations.
// On destruction the arena is rewound to the state at Mark() time.
// NOTE: Objects constructed inside a mark scope that are NOT trivially
// destructible must be manually destroyed before the mark expires.
class ArenaMark {
 public:
  ArenaMark(ArenaMark&&);
  ArenaMark& operator=(ArenaMark&&);
  ~ArenaMark();

  void Rewind();

 private:
  DISABLE_COPY_AND_ASSIGN(ArenaMark);

  friend class Arena;

  ArenaMark(Arena* arena, Block* block, std::size_t used);

  Arena* arena_;
  // Current block at mark time.
  Block* block_;
  // Used offset in that block at mark time.
  std::size_t used_;
  bool valid_;
};

class Arena {
 public:
  typedef std::shared_ptr<Arena> Ptr;
  typedef std::shared_ptr<const Arena> PtrConst;

  explicit Arena(std::size_t initial_size = 64U * 1024U);

  Arena(Arena&& other);

  Arena& operator=(Arena&& other);

  ~Arena();

  void* AllocBytes(std::size_t size,
                   std::size_t align = alignof(std::max_align_t));

  // Construct an object of type T in the arena using placement-new.
  // T's destructor will NOT be called automatically — caller is responsible.
  template<typename T, typename... Args>
  T* Alloc(Args&&... args) {
    static_assert(alignof(T) <= 4096U, "Arena: alignment exceeds maximum");
    void* mem = AllocBytes(sizeof(T), alignof(T));
    return ::new (mem) T(std::forward<Args>(args)...);
  }

  // Allocate a contiguous array of 'count' default-constructed T objects.
  template<typename T>
  T* AllocArray(std::size_t count) {
    if (count == 0U) return nullptr;
    static_assert(alignof(T) <= 4096U, "Arena: alignment exceeds maximum");

    constexpr std::size_t max_count =
      std::numeric_limits<std::size_t>::max() / sizeof(T);
    CHECK(count <= max_count) << "Arena: bad allocation";

    return static_cast<T*>(
      AllocBytes(sizeof(T) * count, alignof(T)));
  }

  void Reset();

  ArenaMark Mark() {
    return ArenaMark(this, current_, current_->used);
  }

  std::size_t total_allocated() const { return total_allocated_; }
  std::size_t total_wasted() const { return total_wasted_; }

  std::size_t reserved_bytes() const {
    std::size_t total = 0U;
    for (const Block* b = current_; b; b = b->next)
      total += b->capacity;
    return total;
  }

  std::size_t block_count() const {
    std::size_t n = 0U;
    for (const Block* b = current_; b; b = b->next) ++n;
    return n;
  }

  void set_growth_factor(std::size_t factor) {
    block_growth_factor_ = (factor < 1U) ? 1U : factor;
  }

 private:
  static Block* MakeBlock(std::size_t minimum_capacity);

  static void* MallocPtrOf(Block* blk);

  static void FreeAllBlocks(Block* blk);

  void RewindTo(Block* mark_block, std::size_t mark_used);

 private:
  DISALLOW_COPY_AND_ASSIGN(Arena);

  friend class ArenaMark;

  Block* current_;
  std::size_t initial_block_size_;
  std::size_t block_growth_factor_;
  std::size_t total_allocated_;
  std::size_t total_wasted_;
};

}} // namespace utils::memory

#endif // UTILS_MEMORY_ARENA_H
