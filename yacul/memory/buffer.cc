#include "yacul/memory/buffer.h"

using namespace std;

namespace utils {
namespace memory {

//------------------------------------------------------------------------------

Buffer Buffer::Create(size_t capacity) {
  Buffer b;
  b.physical_ = PhysicalBuffer::Create(capacity);
  b.data_ = b.physical_->buffer_start();
  b.length_ = 0;
  return b;
}

//------------------------------------------------------------------------------

Buffer Buffer::CopyFrom(const void *src, size_t len) {
  Buffer b = Create(len);
  if (len) {
    memcpy(b.physical_->buffer_start(), src, len);
    b.length_ = len;
  }
  return b;
}

//------------------------------------------------------------------------------

Buffer Buffer::CopyFrom(string str) { return CopyFrom(str.data(), str.size()); }

//------------------------------------------------------------------------------

Buffer Buffer::WrapExternal(void *data, size_t len, void (*free_fn)(void *)) {
  Buffer b;
  b.physical_ = PhysicalBuffer::CreateExternal(data, len, free_fn);
  b.data_ = static_cast<uint8_t *>(data);
  b.length_ = len;
  return b;
}

//------------------------------------------------------------------------------

Buffer::Buffer() { MakeSelfRing(); }

//------------------------------------------------------------------------------

Buffer::~Buffer() {
  UnlinkFromRing();
  ReleasePhysical();
}

//------------------------------------------------------------------------------

Buffer::Buffer(const Buffer &rhs)
  : physical_(rhs.physical_), data_(rhs.data_), length_(rhs.length_) {
  if (physical_)
    physical_->Retain();
  MakeSelfRing();
}

//------------------------------------------------------------------------------

Buffer &Buffer::operator=(const Buffer &rhs) {
  if (this == &rhs)
    return *this;
  UnlinkFromRing();
  ReleasePhysical();
  physical_ = rhs.physical_;
  data_ = rhs.data_;
  length_ = rhs.length_;
  if (physical_)
    physical_->Retain();
  MakeSelfRing();
  return *this;
}

//------------------------------------------------------------------------------

Buffer::Buffer(Buffer &&rhs)
  : physical_(rhs.physical_), data_(rhs.data_), length_(rhs.length_) {
  StealRingPosition(rhs);
  rhs.physical_ = nullptr;
  rhs.data_ = nullptr;
  rhs.length_ = 0;
  rhs.MakeSelfRing();
}

//------------------------------------------------------------------------------

Buffer &Buffer::operator=(Buffer &&rhs) {
  if (this == &rhs)
    return *this;
  UnlinkFromRing();
  ReleasePhysical();
  physical_ = rhs.physical_;
  data_ = rhs.data_;
  length_ = rhs.length_;
  StealRingPosition(rhs);
  rhs.physical_ = nullptr;
  rhs.data_ = nullptr;
  rhs.length_ = 0;
  rhs.MakeSelfRing();
  return *this;
}

//------------------------------------------------------------------------------

void Buffer::AppendChain(Buffer *other) {
  CHECK_NE(other, this);
  // Splice other's ring between prev_ (tail of this) and this (head).
  Buffer *this_tail = prev_;
  Buffer *other_tail = other->prev_;

  this_tail->next_ = other;
  other->prev_ = this_tail;
  other_tail->next_ = this;
  this->prev_ = other_tail;
}

//------------------------------------------------------------------------------

void Buffer::PrependChain(Buffer *other) {
  CHECK_NE(other, this);
  Buffer *other_tail = other->prev_;

  other_tail->next_ = this;
  Buffer *this_prev = this->prev_;
  this_prev->next_ = other;
  other->prev_ = this_prev;
  this->prev_ = other_tail;
}

//------------------------------------------------------------------------------

Buffer *Buffer::Unlink() {
  if (!is_chained())
    return nullptr;
  Buffer *nxt = next_;
  UnlinkFromRing();
  MakeSelfRing();
  return nxt;
}

//------------------------------------------------------------------------------

Buffer *Buffer::SplitChainAfter() {
  if (!is_chained())
    return nullptr;

  //  Original ring pointers at the two cut edges:
  //    this -> rh   (forward cut)
  //    lh   -> this (backward side; lh = this->prev_ ONLY in 2-node ring —
  //                  in general lh is somewhere else, but rt = lh->prev
  //                  when lh is the node just before rh going backwards)
  //
  //  Concretely for A<->B<->C<->A, split after B (this=B):
  //    rh = C,  rt = A  (rt = lh->prev  where lh = A = this->prev_... wait)
  //
  //  In ring A<->B<->C<->A:
  //    A.next=B, A.prev=C
  //    B.next=C, B.prev=A
  //    C.next=A, C.prev=B
  //  Split after B: left={A,B}, right={C}
  //    lh = A = B.prev_                    (first node of left ring)
  //    rt = C = B.prev_.prev_ = A.prev_    (last node of right ring)
  //    rh = C = B.next_                    (first node of right ring)
  //
  //  Relinks needed:
  //    Left ring:  this->next_ = lh;  lh->prev_ = this;
  //    Right ring: rt->next_ = rh;    rh->prev_ = rt;   (rt==rh in 1-node
  //    case)

  Buffer *rh = this->next_; // first node of right ring
  Buffer *lh = this->prev_; // first node of left ring
  Buffer *rt = lh->prev_;   // last  node of right ring

  // Close left ring.
  this->next_ = lh;
  lh->prev_ = this;

  // Close right ring.
  rt->next_ = rh;
  rh->prev_ = rt;

  return rh;
}

//------------------------------------------------------------------------------

Buffer Buffer::CoalesceChain() const {
  const size_t total = ComputeChainDataLength();
  if (total == 0)
    return Buffer{};
  Buffer out = Create(total);
  uint8_t *dst = out.physical_->buffer_start();
  const Buffer *cur = this;
  do {
    memcpy(dst, cur->data_, cur->length_);
    dst += cur->length_;
    cur = cur->next_;
  } while (cur != this);
  out.data_ = out.physical_->buffer_start();
  out.length_ = total;
  return out;
}

//------------------------------------------------------------------------------

vector<uint8_t> Buffer::ToVector() const {
  vector<uint8_t> out;
  out.reserve(ComputeChainDataLength());
  const Buffer *cur = this;
  do {
    out.insert(out.end(), cur->data_, cur->data_ + cur->length_);
    cur = cur->next_;
  } while (cur != this);
  return out;
}

//------------------------------------------------------------------------------

void Buffer::DestroyChain(Buffer *head) {
  if (!head)
    return;
  Buffer *cur = head;
  do {
    Buffer *nxt = cur->next_;
    cur->MakeSelfRing(); // prevent UnlinkFromRing in destructor
    delete cur;
    cur = nxt;
  } while (cur != head);
}

//------------------------------------------------------------------------------

void Buffer::EnsureTailroom(size_t n) {
  if (!is_shared() && tailroom() >= n)
    return;
  const size_t current_head = headroom();
  const size_t new_cap = max(length_ + n, (length_ + n) * 3 / 2 + 64);
  auto *new_phys = PhysicalBuffer::Create(new_cap);
  uint8_t *new_start = new_phys->buffer_start() + current_head;
  if (length_)
    memcpy(new_start, data_, length_);
  ReleasePhysical();
  physical_ = new_phys;
  data_ = new_start;
}

//------------------------------------------------------------------------------

void Buffer::Reserve(size_t new_capacity) {
  if (capacity() >= new_capacity && !is_shared())
    return;
  EnsureTailroom(new_capacity > length_ ? new_capacity - length_ : 0);
}

//------------------------------------------------------------------------------

Buffer Buffer::Slice(size_t offset, size_t len) const {
  CHECK_LE(offset + len, length_) << "Buffer::Slice: out of range";
  Buffer b;
  b.physical_ = physical_;
  b.data_ = data_ + offset;
  b.length_ = len;
  if (physical_)
    physical_->Retain();
  return b;
}

//------------------------------------------------------------------------------

Buffer Buffer::Slice(size_t offset) const {
  return Slice(offset, length_ - offset);
}

//------------------------------------------------------------------------------

Buffer Buffer::Clone() const {
  if (!physical_ || length_ == 0)
    return Buffer{};
  return CopyFrom(data_, length_);
}

//------------------------------------------------------------------------------

void Buffer::MakeSelfRing() { next_ = prev_ = this; }

//------------------------------------------------------------------------------

void Buffer::UnlinkFromRing() {
  prev_->next_ = next_;
  next_->prev_ = prev_;
  // Leave next_/prev_ dangling — caller must call MakeSelfRing after.
}

//------------------------------------------------------------------------------

void Buffer::StealRingPosition(Buffer &rhs) {
  if (!rhs.is_chained()) {
    MakeSelfRing();
    return;
  }
  next_ = rhs.next_;
  prev_ = rhs.prev_;
  next_->prev_ = this;
  prev_->next_ = this;
}

//------------------------------------------------------------------------------

void Buffer::CowDetach() {
  if (!physical_ || !physical_->is_shared())
    return;
  const size_t head = headroom();
  auto *new_phys = PhysicalBuffer::Create(physical_->capacity());
  uint8_t *new_start = new_phys->buffer_start() + head;
  if (length_)
    memcpy(new_start, data_, length_);
  ReleasePhysical();
  physical_ = new_phys;
  data_ = new_start;
}

//------------------------------------------------------------------------------

void Buffer::ReleasePhysical() {
  if (physical_) {
    physical_->Release();
    physical_ = nullptr;
  }
}

//------------------------------------------------------------------------------
// BufferCursor
//------------------------------------------------------------------------------

size_t BufferCursor::TotalLength() const {
  size_t remaining = current_->size() - offset_;
  for (const Buffer *b = current_->next_; b != head_; b = b->next_)
    remaining += b->size();
  return remaining;
}

//------------------------------------------------------------------------------

bool BufferCursor::AtEnd() const {
  return offset_ == current_->size() && current_->next_ == head_;
}

//------------------------------------------------------------------------------

size_t BufferCursor::Read(void *dest, size_t len) {
  uint8_t *out = static_cast<uint8_t *>(dest);
  size_t read = 0;
  while (len > 0 && !AtEnd()) {
    const size_t available = current_->size() - offset_;
    const size_t chunk = min(available, len);
    memcpy(out, current_->data() + offset_, chunk);
    out += chunk;
    read += chunk;
    len -= chunk;
    offset_ += chunk;
    AdvanceIfExhausted();
  }
  return read;
}

//------------------------------------------------------------------------------

void BufferCursor::Skip(size_t len) {
  while (len > 0 && !AtEnd()) {
    const size_t available = current_->size() - offset_;
    const size_t chunk = min(available, len);
    offset_ += chunk;
    len -= chunk;
    AdvanceIfExhausted();
  }
}

//------------------------------------------------------------------------------

size_t BufferCursor::Peek(void *dest, size_t len) const {
  uint8_t *out = static_cast<uint8_t *>(dest);
  size_t read = 0;
  const Buffer *cur = current_;
  size_t off = offset_;

  while (len > 0) {
    const size_t available = cur->size() - off;
    if (available == 0) {
      cur = cur->next_;
      off = 0;
      if (cur == head_)
        break;
    }
    const size_t chunk = min(available, len);
    memcpy(out, cur->data() + off, chunk);
    out += chunk;
    read += chunk;
    len -= chunk;
    off += chunk;
  }
  return read;
}

//------------------------------------------------------------------------------

void BufferCursor::Reset() {
  current_ = head_;
  offset_ = 0;
}

//------------------------------------------------------------------------------

void BufferCursor::AdvanceIfExhausted() {
  if (offset_ == current_->size() && current_->next_ != head_) {
    current_ = current_->next_;
    offset_ = 0;
  }
}

//------------------------------------------------------------------------------
// BufferRWCursor
//------------------------------------------------------------------------------

size_t BufferRWCursor::TotalLength() const {
  size_t remaining = current_->size() - offset_;
  for (const Buffer *b = current_->next_; b != head_; b = b->next_)
    remaining += b->size();
  return remaining;
}

//------------------------------------------------------------------------------

bool BufferRWCursor::AtEnd() const {
  return offset_ == current_->size() && current_->next_ == head_;
}

//------------------------------------------------------------------------------

size_t BufferRWCursor::Write(const void *src, size_t len) {
  const uint8_t *in = static_cast<const uint8_t *>(src);
  size_t written = 0;
  while (len > 0 && !AtEnd()) {
    current_->CowDetach();
    const size_t available = current_->size() - offset_;
    const size_t chunk = min(available, len);
    memcpy(current_->data_ + offset_, in, chunk);
    in += chunk;
    written += chunk;
    len -= chunk;
    offset_ += chunk;
    AdvanceIfExhausted();
  }
  return written;
}

//------------------------------------------------------------------------------

void BufferRWCursor::Skip(size_t len) {
  while (len > 0 && !AtEnd()) {
    const size_t available = current_->size() - offset_;
    const size_t chunk = min(available, len);
    offset_ += chunk;
    len -= chunk;
    AdvanceIfExhausted();
  }
}

//------------------------------------------------------------------------------

void BufferRWCursor::Reset() {
  current_ = head_;
  offset_ = 0;
}

//------------------------------------------------------------------------------

void BufferRWCursor::AdvanceIfExhausted() {
  if (offset_ == current_->size() && current_->next_ != head_) {
    current_ = current_->next_;
    offset_ = 0;
  }
}

//------------------------------------------------------------------------------

} // namespace memory
} // namespace utils
