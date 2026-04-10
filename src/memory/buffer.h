#ifndef UTILS_MEMORY_BUFFER_H
#define UTILS_MEMORY_BUFFER_H

#include "basic/basic.h"
#include "logging/logger.h"

#include <atomic> // atomic
#include <memory> // shared_ptr
#include <vector> // vector

namespace utils {
namespace memory {

/*
 * Buffer is a smart buffer class supporting:
 *   - Copy-on-Write (CoW) for efficient sharing
 *   - Chaining (doubly-linked circular ring)
 *   - Slicing and cloning
 *   - External memory wrapping
 *
 * DESIGN:
 *
 *   PhysicalBuffer (refcounted backing store):
 *   +------------------------+
 *   | ref_count | capacity   |  <-- header (one malloc)
 *   +------------------------+
 *   | .... data bytes ....   |  <-- capacity bytes
 *   +------------------------+
 *
 *   Buffer (logical view on PhysicalBuffer):
 *   +----------------------+
 *   | PhysicalBuffer*     | ----> (shared, refcounted)
 *   | data ptr            | ----> points into PhysicalBuffer
 *   | length              |
 *   | next_ | prev_       | ----> ring links
 *   +----------------------+
 *
 *   Chain (doubly-linked circular ring):
 *   +-------+     +-------+     +-------+
 *   |   A   | <-> |   B   | <-> |   C   |
 *   +-------+     +-------+     +-------+
 *     ^                               |
 *     +---------------circular--------+
 *
 *   Slice (shares PhysicalBuffer, O(1)):
 *   Original: [|----data----|----length----|-----capacity-----]
 *   Slice:               [|--offset--|--len--]
 *
 * PHYSICALBUFFER (reference-counted backing store):
 *   - Create(capacity): allocates header + data in one malloc
 *   - CreateExternal(): wraps external memory with custom free function
 *   - Retain()/Release(): atomic refcounting
 *   - is_shared(): true if refcount > 1
 *
 * BUFFER (logical view):
 *   - Create(capacity): new buffer with fresh PhysicalBuffer
 *   - CopyFrom(src, len): copy external data
 *   - WrapExternal(data, len, free_fn): wrap existing memory
 *
 *   Observers:
 *     - data(), size(), empty()
 *     - headroom() / tailroom() (unused space at ends)
 *     - capacity() (total physical buffer size)
 *     - is_shared() (if backed by shared PhysicalBuffer)
 *
 *   Chain operations:
 *     - is_chained() / next() / prev()
 *     - AppendChain(other): append other's ring to this
 *     - PrependChain(other): prepend other's ring before this
 *     - Unlink(): detach from ring, make standalone
 *     - SplitChainAfter(): divide ring into two
 *     - CoalesceChain(): merge chain into single Buffer
 *     - DestroyChain(head): destroy entire ring
 *
 *   Mutation:
 *     - writable_data(): triggers CoW if shared
 *     - TrimStart(n) / TrimEnd(n): shrink logical view
 *     - Prepend(n): expose headroom into logical view
 *     - Append(n): expose tailroom into logical view
 *     - AppendData(src, n): append raw bytes
 *     - EnsureTailroom(n): grow capacity if needed
 *     - Reserve(new_cap): ensure minimum capacity
 *     - Slice(offset, len): O(1) slice (shares storage)
 *     - Clone(): O(n) independent copy
 *
 * BUFFERCURSOR (read-only):
 *   - Walks transparently across a chain of Buffers
 *   - TotalLength(): total bytes remaining in chain
 *   - AtEnd(): cursor at the end of chain?
 *   - Read(dest, len): read and advance
 *   - Skip(len): advance cursor
 *   - Peek(dest, len): read without advancing
 *   - Reset(): go back to beginning
 *
 * BUFFERRWCURSOR (read-write):
 *   - Same as BufferCursor but writes trigger CoW
 *   - Write(src, len): write and advance
 *   - Skip(len): advance cursor
 *   - Reset(): go back to beginning
 *
 * USAGE EXAMPLES:
 *
 *   1. Simple buffer:
 *      auto buf = Buffer::Create(1024);
 *      buf->AppendData("hello", 5);
 *
 *   2. Wrap external memory:
 *      auto buf = Buffer::WrapExternal(mydata, mysize, free_fn);
 *
 *   3. Chain buffers:
 *      Buffer a = Buffer::Create(100);
 *      Buffer b = Buffer::Create(100);
 *      a.AppendChain(&b);  // a's ring now includes b
 *
 *   4. Slice (O(1), shares storage):
 *      Buffer slice = buf.Slice(10, 20);
 *
 *   5. Clone (O(n), independent copy):
 *      Buffer copy = buf.Clone();
 *
 *   6. Read across chain:
 *      BufferCursor cursor(&head);
 *      cursor.Read(buffer, 100);
 *
 *   7. Write across chain:
 *      BufferRWCursor writer(&head);
 *      writer.Write(data, len);
 *
 * IMPORTANT NOTES:
 *   - Buffer uses CoW: writable_data() may copy if refcount > 1
 *   - Chains are circular rings: next() wraps around
 *   - DestroyChain() is the only safe way to delete a chain
 *   - Slice shares PhysicalBuffer: modifications affect original
 *   - Clone creates independent copy
 *   - headroom/tailroom allow zero-copy prepend/append
 *
 * THREAD SAFETY:
 *   - PhysicalBuffer refcounting is thread-safe
 *   - Buffer mutation is NOT thread-safe (external sync required)
 *
 */

// Forward declarations
class Buffer;
class BufferCursor;
class BufferRWCursor;

// ===========================================================================
// Single combined allocation:
//   [ PhysicalBuffer header | .... capacity bytes .... ]
// ===========================================================================
class PhysicalBuffer {
public:
  // Combined allocation: header + capacity bytes in one malloc.
  static PhysicalBuffer *Create(std::size_t capacity) {
    const std::size_t alloc_size = sizeof(PhysicalBuffer) + capacity;
    void *raw = std::malloc(alloc_size);
    CHECK(raw) << "Buffer: malloc failed for " << LOGVARS(alloc_size);
    return ::new (raw) PhysicalBuffer(capacity);
  }

  // External memory: only the header is allocated here.
  static PhysicalBuffer *CreateExternal(void *external_data,
                                        std::size_t capacity,
                                        void (*free_fn)(void *) = nullptr) {
    void *raw = std::malloc(sizeof(PhysicalBuffer));
    CHECK(raw) << "Buffer: malloc failed for "
               << LOGVARS(sizeof(PhysicalBuffer));
    return ::new (raw)
      PhysicalBuffer(static_cast<uint8_t *>(external_data), capacity, free_fn);
  }

  void Retain() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  // Returns true if this was the last reference and memory was freed.
  bool Release() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      Destroy();
      return true;
    }
    return false;
  }

  uint32_t ref_count() const {
    return ref_count_.load(std::memory_order_relaxed);
  }
  bool is_shared() const { return ref_count() > 1; }
  std::size_t capacity() const { return capacity_; }

  // Pointer to the first usable byte of this physical allocation.
  uint8_t *buffer_start() const {
    if (flags_ & kFlagExternal)
      return external_data_;
    return reinterpret_cast<uint8_t *>(const_cast<PhysicalBuffer *>(this)) +
           sizeof(PhysicalBuffer);
  }

private:
  DISALLOW_COPY_AND_ASSIGN(PhysicalBuffer);
  DISALLOW_MOVE_AND_ASSIGN(PhysicalBuffer);

  static constexpr uint32_t kFlagExternal = 1U << 0;

  explicit PhysicalBuffer(std::size_t capacity)
    : ref_count_(1), capacity_(capacity), flags_(0), external_data_(nullptr),
      free_fn_(nullptr) {}

  PhysicalBuffer(uint8_t *external_data, std::size_t capacity,
                 void (*free_fn)(void *))
    : ref_count_(1), capacity_(capacity), flags_(kFlagExternal),
      external_data_(external_data), free_fn_(free_fn) {}

  ~PhysicalBuffer() = default;

  void Destroy() {
    if ((flags_ & kFlagExternal) && free_fn_)
      free_fn_(external_data_);
    this->~PhysicalBuffer();
    std::free(this);
  }

  std::atomic<uint32_t> ref_count_;
  std::size_t capacity_;
  uint32_t flags_;
  uint8_t *external_data_;
  void (*free_fn_)(void *);
};

// ===========================================================================
// Logical view: (PhysicalBuffer*, data*, length).
// Chaining:     doubly-linked circular ring via next_/prev_.
//
//   head -> [A] <-> [B] <-> [C] <-> (back to A)
//
// Every Buffer is always part of a ring. A standalone Buffer is a ring of
// one (next_ == prev_ == this).
//
// Ownership of the ring is external — the caller that created the chain
// is responsible for its lifetime.  Buffer::~Buffer unlinks self from the
// ring and releases its PhysicalBuffer reference, but does NOT recursively
// destroy the chain.  To destroy a whole chain use DestroyChain().
//
// Copy semantics: copies the logical view only (single node, no chain).
// Move semantics: transfers the node including its ring position.
// ===========================================================================
class Buffer {
public:
  typedef std::shared_ptr<Buffer> Ptr;
  typedef std::shared_ptr<const Buffer> PtrConst;

  // -------------------------------------------------------------------------
  // Factory methods
  // -------------------------------------------------------------------------
  static Buffer Create(std::size_t capacity);

  static Buffer CopyFrom(const void *src, std::size_t len);

  static Buffer CopyFrom(std::string str);

  static Buffer WrapExternal(void *data, std::size_t len,
                             void (*free_fn)(void *) = nullptr);

  // -------------------------------------------------------------------------
  // Constructors / destructor
  // -------------------------------------------------------------------------

  Buffer();
  ~Buffer();

  // Copy: single-node copy of the logical view, not part of the source chain.
  Buffer(const Buffer &rhs);
  Buffer &operator=(const Buffer &rhs);

  // Move: steal the node and its ring position.
  Buffer(Buffer &&rhs);
  Buffer &operator=(Buffer &&rhs);

  // -------------------------------------------------------------------------
  // Single-node observers
  // -------------------------------------------------------------------------

  const uint8_t *data() const { return data_; }
  std::size_t size() const { return length_; }
  bool empty() const { return length_ == 0; }

  std::size_t headroom() const {
    if (!physical_)
      return 0;
    return static_cast<std::size_t>(data_ - physical_->buffer_start());
  }

  std::size_t tailroom() const {
    if (!physical_)
      return 0;
    return physical_->capacity() - headroom() - length_;
  }

  std::size_t capacity() const { return physical_ ? physical_->capacity() : 0; }

  bool is_shared() const { return physical_ && physical_->is_shared(); }

  // -------------------------------------------------------------------------
  // Chain observers
  // -------------------------------------------------------------------------

  bool is_chained() const { return next_ != this; }
  Buffer *next() { return next_; }
  const Buffer *next() const { return next_; }
  Buffer *prev() { return prev_; }
  const Buffer *prev() const { return prev_; }

  // Total bytes across all nodes in the chain.
  std::size_t ComputeChainDataLength() const {
    std::size_t total = length_;
    for (const Buffer *cur = next_; cur != this; cur = cur->next_)
      total += cur->length_;
    return total;
  }

  // Number of nodes in the chain (including this).
  std::size_t CountChainElements() const {
    std::size_t count = 1;
    for (const Buffer *cur = next_; cur != this; cur = cur->next_)
      ++count;
    return count;
  }

  // -------------------------------------------------------------------------
  // Chain mutation
  // -------------------------------------------------------------------------

  // Append `other` and its entire ring to the end of this ring.
  // `other` must not already be in this ring.
  void AppendChain(Buffer *other);

  // Prepend `other` and its entire ring before this node.
  void PrependChain(Buffer *other);

  // Unlink this node from its ring and make it a standalone ring.
  // Returns pointer to the next node in the original ring (or nullptr if
  // this was already standalone).
  Buffer *Unlink();

  // Split the chain after this node.
  // Given a ring: [lh <-> ... <-> this <-> rh <-> ... <-> rt <-> lh]
  // Produces two rings:
  //   Left  ring: [lh <-> ... <-> this <-> lh]   (caller already holds lh)
  //   Right ring: [rh <-> ... <-> rt <-> rh]     (returned)
  //
  // Returns the head of the right ring (original next_ of this node).
  Buffer *SplitChainAfter();

  // Coalesce the entire chain into a single Buffer and return it.
  // The original chain nodes are NOT destroyed — caller is responsible.
  Buffer CoalesceChain() const;

  // Gather all chain data into a std::vector<uint8_t> (convenience).
  std::vector<uint8_t> ToVector() const;

  // Destroy the entire chain starting from this node.
  // Do NOT use any Buffer pointer that was in the chain after this call.
  static void DestroyChain(Buffer *head);

  // -------------------------------------------------------------------------
  // Single-node mutation (CoW-aware)
  // -------------------------------------------------------------------------

  uint8_t *writable_data() {
    CowDetach();
    return data_;
  }

  void TrimStart(std::size_t n) {
    CHECK_LE(n, length_) << "Buffer: TrimStart out of range";
    data_ += n;
    length_ -= n;
  }

  void TrimEnd(std::size_t n) {
    CHECK_LE(n, length_) << "Buffer: TrimEnd out of range";
    length_ -= n;
  }

  // Expose n bytes of headroom into the logical view.
  void Prepend(std::size_t n) {
    CHECK_LE(n, headroom()) << "Buffer: Prepend out of range";
    data_ -= n;
    length_ += n;
  }

  // Expose n bytes of tailroom into the logical view.
  void Append(std::size_t n) {
    CHECK_LE(n, tailroom()) << "Buffer: Append out of range";
    length_ += n;
  }

  void AppendData(const void *src, std::size_t n) {
    EnsureTailroom(n);
    memcpy(data_ + length_, src, n);
    length_ += n;
  }

  void AppendData(std::string str) { AppendData(str.data(), str.size()); }

  void EnsureTailroom(std::size_t n);

  void Reserve(std::size_t new_capacity);

  // O(1) slice — shares PhysicalBuffer, single node (not chained).
  Buffer Slice(std::size_t offset, std::size_t len) const;

  Buffer Slice(std::size_t offset) const;

  // O(n) independent copy of logical view.
  Buffer Clone() const;

  // -------------------------------------------------------------------------
  // Comparison (single node only)
  // -------------------------------------------------------------------------

  bool operator==(const Buffer &rhs) const {
    if (length_ != rhs.length_)
      return false;
    if (data_ == rhs.data_)
      return true;
    return std::memcmp(data_, rhs.data_, length_) == 0;
  }

  bool operator!=(const Buffer &rhs) const { return !(*this == rhs); }

  std::string ToString() const {
    return {reinterpret_cast<const char *>(data_), length_};
  }

private:
  friend class BufferCursor;
  friend class BufferRWCursor;

  // -------------------------------------------------------------------------
  // Ring helpers
  // -------------------------------------------------------------------------

  void MakeSelfRing();

  void UnlinkFromRing();

  // Steal rhs's position in the ring; rhs is left unlinked (dangling).
  void StealRingPosition(Buffer &rhs);

  // -------------------------------------------------------------------------
  // Copy On Write helpers
  // -------------------------------------------------------------------------

  void CowDetach();

  void ReleasePhysical();

  // Hot fields first for cache locality.

  // Backing store
  PhysicalBuffer *physical_{nullptr};
  // Start of logical view
  uint8_t *data_{nullptr};
  // Bytes in logical view
  std::size_t length_{0};
  // Ring successor
  Buffer *next_{nullptr};
  // Ring predecessor
  Buffer *prev_{nullptr};
};

// ===========================================================================
// BufferCursor — read-only cursor that walks a Buffer chain transparently.
// ===========================================================================
class BufferCursor {
public:
  explicit BufferCursor(const Buffer *head)
    : head_(head), current_(head), offset_(0) {}

  // Bytes remaining in the entire chain from current position.
  std::size_t TotalLength() const;

  bool AtEnd() const;

  // Read up to `len` bytes into `dest`. Returns bytes actually read.
  std::size_t Read(void *dest, std::size_t len);

  // Skip `len` bytes.
  void Skip(std::size_t len);

  // Peek at up to `len` bytes without advancing.
  std::size_t Peek(void *dest, std::size_t len) const;

  void Reset();

private:
  void AdvanceIfExhausted();

  const Buffer *head_;
  const Buffer *current_;
  std::size_t offset_;
};

// ===========================================================================
// BufferRWCursor — read-write cursor.
// Triggers CoW on the current node before any write.
// ===========================================================================
class BufferRWCursor {
public:
  explicit BufferRWCursor(Buffer *head)
    : head_(head), current_(head), offset_(0) {}

  std::size_t TotalLength() const;

  bool AtEnd() const;

  // Write `len` bytes from `src`, advancing the cursor.
  // Triggers CoW on each node touched.
  std::size_t Write(const void *src, std::size_t len);

  void Skip(std::size_t len);

  void Reset();

private:
  void AdvanceIfExhausted();

  Buffer *head_;
  Buffer *current_;
  std::size_t offset_;
};

} // namespace memory
} // namespace utils

#endif // UTILS_MEMORY_BUFFER_H
