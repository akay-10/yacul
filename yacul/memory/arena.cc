#include "yacul/memory/arena.h"

using namespace std;

namespace utils {
namespace memory {

//------------------------------------------------------------------------------

Arena::Arena(size_t initial_size)
  : current_(nullptr), initial_block_size_(0U), block_growth_factor_(2U),
    total_allocated_(0U), total_wasted_(0U) {

  CHECK_GT(initial_size, 0U) << "Arena: initial block size must be > 0";
  initial_block_size_ = max(initial_size, sizeof(Block) * 2U);
  current_ = MakeBlock(initial_block_size_);
}

//------------------------------------------------------------------------------

Arena::Arena(Arena &&other)
  : current_(other.current_), initial_block_size_(other.initial_block_size_),
    block_growth_factor_(other.block_growth_factor_),
    total_allocated_(other.total_allocated_),
    total_wasted_(other.total_wasted_) {

  other.current_ = nullptr;
}

//------------------------------------------------------------------------------

Arena &Arena::operator=(Arena &&other) {
  if (this != &other) {
    FreeAllBlocks(current_);
    current_ = other.current_;
    initial_block_size_ = other.initial_block_size_;
    block_growth_factor_ = other.block_growth_factor_;
    total_allocated_ = other.total_allocated_;
    total_wasted_ = other.total_wasted_;
    other.current_ = nullptr;
  }
  return *this;
}

//------------------------------------------------------------------------------

Arena::~Arena() { FreeAllBlocks(current_); }

//------------------------------------------------------------------------------

void *Arena::AllocBytes(size_t size, size_t align) {
  if (size == 0u)
    return nullptr;
  detail::CheckAlignment(align);

  void *ptr = current_->TryAlloc(size, align);
  if (ptr) {
    total_allocated_ += size;
    return ptr;
  }

  // Grow the arena and new block must fit at least 'size' bytes plus
  // header/alignment overhead.
  const size_t header_size =
    detail::AlignUp(sizeof(Block), alignof(max_align_t));
  size_t new_cap = max(current_->capacity * block_growth_factor_,
                       detail::AlignUp(size + header_size, align) + align);

  Block *blk = MakeBlock(new_cap);
  blk->next = current_;
  current_ = blk;

  ptr = current_->TryAlloc(size, align);
  CHECK(ptr) << "Arena: freshly-created block must satisfy allocation";

  total_allocated_ += size;
  return ptr;
}

//------------------------------------------------------------------------------

void Arena::Reset() {
  FreeAllBlocks(current_);
  current_ = MakeBlock(initial_block_size_);
  total_allocated_ = 0U;
  total_wasted_ = 0U;
}

//------------------------------------------------------------------------------

Block *Arena::MakeBlock(size_t minimum_capacity) {
  // We want to use plain malloc/free so that a linked tcmalloc (or any drop-in
  // malloc replacement) is transparently used.
  //
  // Layout of the malloc'd region:
  //  [ AllocHeader ][ padding ][ Block header ][ payload ... ]
  //   ^malloc returns this pointer
  //                             ^aligned to alignof(max_align_t)
  //                             ^this is the Block* we hand out
  //
  // AllocHeader stores the original malloc pointer so we can free it exactly,
  // even after aligning up past it.

  const size_t align = alignof(max_align_t);
  const size_t blk_header = detail::AlignUp(sizeof(Block), align);

  // Worst-case overhead: sizeof(AllocHeader) + up to (align - 1) padding to
  // reach the next aligned boundary for the Block.
  const size_t overhead = detail::AlignUp(sizeof(detail::AllocHeader), align);
  const size_t total = overhead + blk_header + minimum_capacity;

  void *raw = malloc(total);
  CHECK(raw) << "Arena: malloc failed for block of size " << total;

  // The Block starts at raw + overhead (already aligned because overhead
  // is a multiple of align and raw is at least alignof(max_align_t) - aligned
  // as guaranteed by malloc).
  byte *block_ptr = static_cast<byte *>(raw) + overhead;

  // Write the original malloc pointer into the AllocHeader slot that sits
  // exactly 'overhead' bytes before the Block.
  auto *hdr = reinterpret_cast<detail::AllocHeader *>(
    block_ptr - sizeof(detail::AllocHeader));
  hdr->malloc_ptr = raw;

  Block *blk = new (block_ptr) Block{};
  blk->next = nullptr;
  blk->capacity = total - overhead - blk_header;
  blk->used = 0U;
  return blk;
}

//------------------------------------------------------------------------------

void *Arena::MallocPtrOf(Block *blk) {
  auto *hdr = reinterpret_cast<detail::AllocHeader *>(blk) - 1;
  return hdr->malloc_ptr;
}

//------------------------------------------------------------------------------

void Arena::FreeAllBlocks(Block *blk) {
  while (blk) {
    Block *next = blk->next;
    free(MallocPtrOf(blk));
    blk = next;
  }
}

//------------------------------------------------------------------------------

void Arena::RewindTo(Block *mark_block, size_t mark_used) {
  while (current_ != mark_block) {
    Block *older = current_->next;
    free(MallocPtrOf(current_));
    current_ = older;
  }
  current_->used = mark_used;
}

//------------------------------------------------------------------------------

ArenaMark::ArenaMark(Arena *arena, Block *block, size_t used)
  : arena_(arena), block_(block), used_(used), valid_(true) {}

//------------------------------------------------------------------------------

ArenaMark::~ArenaMark() {
  if (valid_)
    Rewind();
}

//------------------------------------------------------------------------------

ArenaMark::ArenaMark(ArenaMark &&o)
  : arena_(o.arena_), block_(o.block_), used_(o.used_), valid_(o.valid_) {
  o.valid_ = false;
}

//------------------------------------------------------------------------------

ArenaMark &ArenaMark::operator=(ArenaMark &&o) {
  if (this != &o) {
    if (valid_)
      Rewind();
    arena_ = o.arena_;
    block_ = o.block_;
    used_ = o.used_;
    valid_ = o.valid_;
    o.valid_ = false;
  }
  return *this;
}

//------------------------------------------------------------------------------

void ArenaMark::Rewind() {
  if (valid_) {
    arena_->RewindTo(block_, used_);
    valid_ = false;
  }
}

//------------------------------------------------------------------------------

} // namespace memory
} // namespace utils
