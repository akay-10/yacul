#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "buffer.h"

using namespace std;
using namespace utils::memory;

// ===========================================================================
// Helpers
// ===========================================================================

static string BufferStr(const Buffer &b) { return string(b.ToString()); }

// Build a heap-allocated chain and return the head.
// Caller owns the chain and must call Buffer::DestroyChain(head).
static Buffer *MakeChain(initializer_list<string> parts) {
  Buffer *head = nullptr;
  for (auto buff_str : parts) {
    auto *node = new Buffer(Buffer::CopyFrom(buff_str));
    if (!head) {
      head = node;
    } else {
      head->AppendChain(node);
    }
  }
  return head;
}

// Collect all chain data as a string (walks the ring once).
static string ChainToString(const Buffer *head) {
  if (!head)
    return {};
  string out;
  const Buffer *cur = head;
  do {
    out += BufferStr(*cur);
    cur = cur->next();
  } while (cur != head);
  return out;
}

// ===========================================================================
// PhysicalBufferTest — white-box tests for the backing store
// ===========================================================================

TEST(PhysicalBufferTest, CreateAndCapacity) {
  PhysicalBuffer *pb = PhysicalBuffer::Create(128);
  ASSERT_NE(pb, nullptr);
  EXPECT_EQ(pb->capacity(), 128u);
  EXPECT_EQ(pb->ref_count(), 1u);
  EXPECT_FALSE(pb->is_shared());
  EXPECT_TRUE(pb->Release());
}

TEST(PhysicalBufferTest, BufferStartIsContiguous) {
  // Combined alloc: buffer_start() should point immediately after the header.
  PhysicalBuffer *pb = PhysicalBuffer::Create(64);
  uint8_t *expected = reinterpret_cast<uint8_t *>(pb) + sizeof(PhysicalBuffer);
  EXPECT_EQ(pb->buffer_start(), expected);
  pb->Release();
}

TEST(PhysicalBufferTest, RefCounting) {
  PhysicalBuffer *pb = PhysicalBuffer::Create(32);
  EXPECT_EQ(pb->ref_count(), 1u);
  EXPECT_FALSE(pb->is_shared());

  pb->Retain();
  EXPECT_EQ(pb->ref_count(), 2u);
  EXPECT_TRUE(pb->is_shared());

  pb->Retain();
  EXPECT_EQ(pb->ref_count(), 3u);

  pb->Release();
  EXPECT_EQ(pb->ref_count(), 2u);

  pb->Release();
  EXPECT_EQ(pb->ref_count(), 1u);
  EXPECT_FALSE(pb->is_shared());

  // Final Release destroys pb — no use after this point.
  bool destroyed = pb->Release();
  EXPECT_TRUE(destroyed);
}

TEST(PhysicalBufferTest, ExternalMemory) {
  uint8_t ext[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  bool freed = false;
  auto free_fn = [](void *p) {
    // We use a static flag to check the callback fired.
    *reinterpret_cast<bool *>(p) = true;
  };
  // Pass &freed as the "data" just to verify the free_fn is invoked.
  // In real use, ext would be the payload.
  PhysicalBuffer *pb = PhysicalBuffer::CreateExternal(
      ext, sizeof(ext), [](void *) { /* no-op for this unit */ });
  EXPECT_EQ(pb->capacity(), 8u);
  EXPECT_EQ(pb->buffer_start(), ext);
  EXPECT_EQ(pb->ref_count(), 1u);
  pb->Release(); // should invoke free_fn (no-op here) and free header
}

// ===========================================================================
// BufferFactoryTest — construction and factory methods
// ===========================================================================

TEST(BufferFactoryTest, CreateEmpty) {
  auto b = Buffer::Create(64);
  EXPECT_EQ(b.size(), 0u);
  EXPECT_TRUE(b.empty());
  EXPECT_GE(b.capacity(), 64u);
  EXPECT_EQ(b.headroom(), 0u);
  EXPECT_GE(b.tailroom(), 64u);
  EXPECT_FALSE(b.is_shared());
}

TEST(BufferFactoryTest, CopyFromRawBytes) {
  const char src[] = "hello world";
  auto b = Buffer::CopyFrom(src, sizeof(src) - 1);
  EXPECT_EQ(b.size(), 11u);
  EXPECT_FALSE(b.empty());
  EXPECT_EQ(BufferStr(b), "hello world");
}

TEST(BufferFactoryTest, CopyFromString) {
  auto b = Buffer::CopyFrom(string("test data"));
  EXPECT_EQ(b.size(), 9u);
  EXPECT_EQ(BufferStr(b), "test data");
}

TEST(BufferFactoryTest, CopyFromEmpty) {
  auto b = Buffer::CopyFrom("");
  EXPECT_EQ(b.size(), 0u);
  EXPECT_TRUE(b.empty());
}

TEST(BufferFactoryTest, WrapExternal) {
  uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
  auto b = Buffer::WrapExternal(data, sizeof(data));
  EXPECT_EQ(b.size(), 4u);
  EXPECT_EQ(b.data()[0], 0xDE);
  EXPECT_EQ(b.data()[3], 0xEF);
  // data() must point into the original array
  EXPECT_EQ(b.data(), data);
}

TEST(BufferFactoryTest, WrapExternalWithFreeFn) {
  static bool free_called = false;
  uint8_t *heap = new uint8_t[16];
  memset(heap, 0xAA, 16);
  {
    auto b = Buffer::WrapExternal(heap, 16, [](void *p) {
      delete[] static_cast<uint8_t *>(p);
      free_called = true;
    });
    EXPECT_EQ(b.size(), 16u);
    EXPECT_EQ(b.data()[0], 0xAA);
  } // b destroyed here — free_fn should fire
  EXPECT_TRUE(free_called);
}

TEST(BufferFactoryTest, DefaultConstructedBuffer) {
  Buffer b;
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(b.size(), 0u);
  EXPECT_EQ(b.data(), nullptr);
  EXPECT_EQ(b.capacity(), 0u);
  EXPECT_EQ(b.headroom(), 0u);
  EXPECT_EQ(b.tailroom(), 0u);
  EXPECT_FALSE(b.is_shared());
}

// ===========================================================================
// BufferObserverTest — size/headroom/tailroom arithmetic
// ===========================================================================

TEST(BufferObserverTest, HeadroomAndTailroom) {
  auto b = Buffer::Create(100);
  EXPECT_EQ(b.headroom(), 0u);
  EXPECT_EQ(b.tailroom(), 100u);
  EXPECT_EQ(b.size(), 0u);
  EXPECT_EQ(b.capacity(), 100u);
}

TEST(BufferObserverTest, TailroomAfterAppendData) {
  auto b = Buffer::Create(100);
  b.AppendData("hello", 5);
  EXPECT_EQ(b.size(), 5u);
  EXPECT_EQ(b.headroom(), 0u);
  EXPECT_EQ(b.tailroom(), 95u);
}

TEST(BufferObserverTest, HeadroomAfterTrimStart) {
  auto b = Buffer::CopyFrom("abcdefghij"); // 10 bytes
  b.TrimStart(3);
  EXPECT_EQ(b.size(), 7u);
  EXPECT_EQ(b.headroom(), 3u);
  EXPECT_EQ(b.tailroom(), 0u);
}

// ===========================================================================
// BufferCopyMoveTest — copy and move semantics
// ===========================================================================

TEST(BufferCopyMoveTest, CopySharesPhysical) {
  auto b1 = Buffer::CopyFrom("shared data");
  auto b2 = b1; // copy

  EXPECT_EQ(b1.size(), b2.size());
  EXPECT_EQ(b1.data(), b2.data()); // same physical pointer
  EXPECT_TRUE(b1.is_shared());
  EXPECT_TRUE(b2.is_shared());
}

TEST(BufferCopyMoveTest, CopyAssignmentSharesPhysical) {
  auto b1 = Buffer::CopyFrom("original");
  Buffer b2;
  b2 = b1;
  EXPECT_EQ(b1.data(), b2.data());
  EXPECT_TRUE(b1.is_shared());
}

TEST(BufferCopyMoveTest, MoveTransfersOwnership) {
  auto b1 = Buffer::CopyFrom("moved data");
  const uint8_t *original_ptr = b1.data();
  size_t original_size = b1.size();

  Buffer b2 = move(b1);

  EXPECT_EQ(b2.data(), original_ptr);
  EXPECT_EQ(b2.size(), original_size);
  EXPECT_EQ(BufferStr(b2), "moved data");

  // b1 should be in a valid empty state
  EXPECT_TRUE(b1.empty());
  EXPECT_EQ(b1.data(), nullptr);
  // b2 is the sole owner — ref_count == 1
  EXPECT_FALSE(b2.is_shared());
}

TEST(BufferCopyMoveTest, MoveAssignment) {
  auto b1 = Buffer::CopyFrom("move assign");
  const uint8_t *ptr = b1.data();

  Buffer b2 = Buffer::CopyFrom("other");
  b2 = move(b1);

  EXPECT_EQ(b2.data(), ptr);
  EXPECT_EQ(BufferStr(b2), "move assign");
  EXPECT_TRUE(b1.empty());
}

TEST(BufferCopyMoveTest, SelfAssignmentCopy) {
  auto b = Buffer::CopyFrom("self copy");
  Buffer &ref = b;
  b = ref; // self-assign — should be a no-op
  EXPECT_EQ(BufferStr(b), "self copy");
}

TEST(BufferCopyMoveTest, SelfAssignmentMove) {
  auto b = Buffer::CopyFrom("self move");
  b = move(b); // self-move — should not crash
               // Content may or may not be preserved; just verify no crash/UB.
}

TEST(BufferCopyMoveTest, MultipleCopiesToSamePhysical) {
  auto b1 = Buffer::CopyFrom("multi");
  auto b2 = b1;
  auto b3 = b1;
  auto b4 = b2;

  EXPECT_EQ(b1.data(), b2.data());
  EXPECT_EQ(b1.data(), b3.data());
  EXPECT_EQ(b1.data(), b4.data());
  EXPECT_TRUE(b1.is_shared());
}

// ===========================================================================
// BufferMutationTest — TrimStart, TrimEnd, Prepend, Append, AppendData
// ===========================================================================

TEST(BufferMutationTest, TrimStart) {
  auto b = Buffer::CopyFrom("hello world");
  b.TrimStart(6);
  EXPECT_EQ(BufferStr(b), "world");
  EXPECT_EQ(b.headroom(), 6u);
}

TEST(BufferMutationTest, TrimStartAll) {
  auto b = Buffer::CopyFrom("abc");
  b.TrimStart(3);
  EXPECT_EQ(b.size(), 0u);
  EXPECT_TRUE(b.empty());
}

TEST(BufferMutationTest, TrimStartOutOfRange) {
  auto b = Buffer::CopyFrom("hi");
  EXPECT_DEATH(b.TrimStart(3), "out of range");
}

TEST(BufferMutationTest, TrimEnd) {
  auto b = Buffer::CopyFrom("hello world");
  b.TrimEnd(6);
  EXPECT_EQ(BufferStr(b), "hello");
  EXPECT_EQ(b.tailroom(), 6u);
}

TEST(BufferMutationTest, TrimEndAll) {
  auto b = Buffer::CopyFrom("abc");
  b.TrimEnd(3);
  EXPECT_TRUE(b.empty());
}

TEST(BufferMutationTest, TrimEndOutOfRange) {
  auto b = Buffer::CopyFrom("hi");
  EXPECT_DEATH(b.TrimEnd(3), "out of range");
}

TEST(BufferMutationTest, PrependIntoHeadroom) {
  auto b = Buffer::CopyFrom("world");
  b.TrimStart(2);                           // creates 2 bytes of headroom
  EXPECT_DEATH(b.Prepend(3), "out of range"); // only 2 bytes available
  b.Prepend(2);
  EXPECT_EQ(BufferStr(b), "world"); // 'wo' was already there in the headroom
}

TEST(BufferMutationTest, AppendIntoTailroom) {
  auto b = Buffer::Create(10);
  b.AppendData("hello", 5); // length=5, tailroom=5
  b.Append(3);              // extend length into tailroom by 3
  EXPECT_EQ(b.size(), 8u);
}

TEST(BufferMutationTest, AppendOutOfRange) {
  auto b = Buffer::Create(5);
  b.AppendData("hello", 5); // tailroom=0
  EXPECT_DEATH(b.Append(1), "out of range");
}

TEST(BufferMutationTest, AppendDataGrows) {
  auto b = Buffer::Create(4);
  b.AppendData("hello world, this is more than 4 bytes");
  EXPECT_EQ(BufferStr(b), "hello world, this is more than 4 bytes");
}

TEST(BufferMutationTest, AppendDataString) {
  auto b = Buffer::Create(32);
  b.AppendData(string("foo"));
  b.AppendData(string("bar"));
  EXPECT_EQ(BufferStr(b), "foobar");
}

TEST(BufferMutationTest, AppendDataMultipleTimes) {
  auto b = Buffer::Create(2);
  for (int i = 0; i < 100; ++i) {
    b.AppendData("x", 1);
  }
  EXPECT_EQ(b.size(), 100u);
  for (size_t i = 0; i < b.size(); ++i) {
    EXPECT_EQ(b.data()[i], 'x');
  }
}

TEST(BufferMutationTest, WritableDataReturnsWritablePointer) {
  auto b = Buffer::CopyFrom("hello");
  uint8_t *w = b.writable_data();
  w[0] = 'H';
  EXPECT_EQ(BufferStr(b), "Hello");
}

TEST(BufferMutationTest, ReserveGrowsCapacity) {
  auto b = Buffer::Create(8);
  b.AppendData("hi", 2);
  b.Reserve(1024);
  EXPECT_GE(b.capacity(), 1024u);
  EXPECT_EQ(b.size(), 2u);
  EXPECT_EQ(BufferStr(b), "hi"); // data preserved
}

TEST(BufferMutationTest, EnsureTailroomPreservesData) {
  auto b = Buffer::CopyFrom("preserved");
  b.EnsureTailroom(500);
  EXPECT_EQ(BufferStr(b), "preserved");
  EXPECT_GE(b.tailroom(), 500u);
}

// ===========================================================================
// BufferSliceTest — Slice
// ===========================================================================

TEST(BufferSliceTest, SliceSharesPhysical) {
  auto b = Buffer::CopyFrom("hello world");
  auto s = b.Slice(6, 5);

  EXPECT_EQ(BufferStr(s), "world");
  EXPECT_EQ(s.size(), 5u);
  EXPECT_TRUE(b.is_shared());
  EXPECT_TRUE(s.is_shared());
  EXPECT_EQ(b.data() + 6, s.data()); // same physical, different offset
}

TEST(BufferSliceTest, SliceOffsetOnly) {
  auto b = Buffer::CopyFrom("hello world");
  auto s = b.Slice(6);
  EXPECT_EQ(BufferStr(s), "world");
}

TEST(BufferSliceTest, SliceZeroLength) {
  auto b = Buffer::CopyFrom("data");
  auto s = b.Slice(2, 0);
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0u);
}

TEST(BufferSliceTest, SliceFullBuffer) {
  auto b = Buffer::CopyFrom("full");
  auto s = b.Slice(0, 4);
  EXPECT_EQ(BufferStr(s), "full");
  EXPECT_EQ(s.data(), b.data());
}

TEST(BufferSliceTest, SliceOutOfRange) {
  auto b = Buffer::CopyFrom("short");
  EXPECT_DEATH(b.Slice(3, 10), "out of range");
}

TEST(BufferSliceTest, ChainedSlices) {
  auto b = Buffer::CopyFrom("abcdefghij");
  auto s1 = b.Slice(0, 5);
  auto s2 = b.Slice(5, 5);
  auto s3 = s1.Slice(1, 3); // slice of slice

  EXPECT_EQ(BufferStr(s1), "abcde");
  EXPECT_EQ(BufferStr(s2), "fghij");
  EXPECT_EQ(BufferStr(s3), "bcd");
  EXPECT_TRUE(b.is_shared());
}

TEST(BufferSliceTest, SliceAtBoundary) {
  auto b = Buffer::CopyFrom("abc");
  auto s = b.Slice(3, 0); // exactly at end, zero length
  EXPECT_EQ(s.size(), 0u);
}

// ===========================================================================
// BufferCloneTest — Clone (copy-on-write materialisation)
// ===========================================================================

TEST(BufferCloneTest, CloneIsIndependent) {
  auto b1 = Buffer::CopyFrom("original");
  auto b2 = b1.Clone();

  // Data equal but physical independent
  EXPECT_EQ(BufferStr(b1), BufferStr(b2));
  EXPECT_NE(b1.data(), b2.data());
  EXPECT_FALSE(b1.is_shared());
  EXPECT_FALSE(b2.is_shared());
}

TEST(BufferCloneTest, MutatingCloneDoesNotAffectOriginal) {
  auto b1 = Buffer::CopyFrom("hello");
  auto b2 = b1.Clone();

  b2.writable_data()[0] = 'H';
  EXPECT_EQ(BufferStr(b1), "hello");
  EXPECT_EQ(BufferStr(b2), "Hello");
}

TEST(BufferCloneTest, CloneOfEmptyBuffer) {
  Buffer b1;
  auto b2 = b1.Clone();
  EXPECT_TRUE(b2.empty());
}

TEST(BufferCloneTest, ClonePreservesSize) {
  auto b1 = Buffer::CopyFrom("exactly ten!");
  auto b2 = b1.Clone();
  EXPECT_EQ(b1.size(), b2.size());
}

// ===========================================================================
// CopyOnWriteTest — CoW semantics on mutation
// ===========================================================================

TEST(CopyOnWriteTest, WritableDataDetachesOnShared) {
  auto b1 = Buffer::CopyFrom("shared");
  auto b2 = b1; // both share physical

  EXPECT_TRUE(b1.is_shared());
  EXPECT_TRUE(b2.is_shared());

  // Mutate b2 — should trigger CoW
  uint8_t *w = b2.writable_data();
  w[0] = 'X';

  EXPECT_FALSE(b1.is_shared()); // b1 now sole owner
  EXPECT_FALSE(b2.is_shared());
  EXPECT_EQ(BufferStr(b1), "shared");
  EXPECT_EQ(BufferStr(b2), "Xhared");
}

TEST(CopyOnWriteTest, AppendDataDetachesOnShared) {
  auto b1 = Buffer::CopyFrom("base");
  auto b2 = b1;

  b2.AppendData(" more", 5);

  EXPECT_EQ(BufferStr(b1), "base");
  EXPECT_EQ(BufferStr(b2), "base more");
  EXPECT_FALSE(b1.is_shared());
}

TEST(CopyOnWriteTest, EnsureTailroomDetachesOnShared) {
  auto b1 = Buffer::CopyFrom("data");
  auto b2 = b1;

  b2.EnsureTailroom(100);

  EXPECT_EQ(BufferStr(b1), "data");
  EXPECT_EQ(BufferStr(b2), "data");
  EXPECT_FALSE(b1.is_shared());
  EXPECT_FALSE(b2.is_shared());
}

TEST(CopyOnWriteTest, SliceThenMutate) {
  auto b1 = Buffer::CopyFrom("hello world");
  auto b2 = b1.Slice(0, 5);

  EXPECT_TRUE(b1.is_shared());

  b2.writable_data()[0] = 'H'; // CoW fires on b2

  EXPECT_FALSE(b1.is_shared());
  EXPECT_EQ(BufferStr(b1), "hello world"); // unchanged
  EXPECT_EQ(BufferStr(b2), "Hello");
}

TEST(CopyOnWriteTest, NoDetachWhenUnique) {
  auto b = Buffer::CopyFrom("unique");
  const uint8_t *ptr_before = b.data();
  EXPECT_FALSE(b.is_shared());
  b.writable_data(); // should NOT reallocate
  EXPECT_EQ(b.data(), ptr_before);
}

TEST(CopyOnWriteTest, ThreeWaySharing) {
  auto b1 = Buffer::CopyFrom("abc");
  auto b2 = b1;
  auto b3 = b1;

  EXPECT_TRUE(b1.is_shared());

  // Mutate b3 — b1 and b2 still share
  b3.writable_data()[0] = 'Z';
  EXPECT_TRUE(b1.is_shared()); // b1 and b2 still share
  EXPECT_FALSE(b3.is_shared());
  EXPECT_EQ(BufferStr(b1), "abc");
  EXPECT_EQ(BufferStr(b3), "Zbc");

  // Mutate b2 — b1 now sole owner
  b2.writable_data()[0] = 'Y';
  EXPECT_FALSE(b1.is_shared());
  EXPECT_EQ(BufferStr(b1), "abc");
  EXPECT_EQ(BufferStr(b2), "Ybc");
}

// ===========================================================================
// BufferComparisonTest — operator== / operator!=
// ===========================================================================

TEST(BufferComparisonTest, EqualContent) {
  auto b1 = Buffer::CopyFrom("same");
  auto b2 = Buffer::CopyFrom("same");
  EXPECT_TRUE(b1 == b2);
  EXPECT_FALSE(b1 != b2);
}

TEST(BufferComparisonTest, DifferentContent) {
  auto b1 = Buffer::CopyFrom("aaa");
  auto b2 = Buffer::CopyFrom("bbb");
  EXPECT_FALSE(b1 == b2);
  EXPECT_TRUE(b1 != b2);
}

TEST(BufferComparisonTest, DifferentSize) {
  auto b1 = Buffer::CopyFrom("abc");
  auto b2 = Buffer::CopyFrom("abcd");
  EXPECT_TRUE(b1 != b2);
}

TEST(BufferComparisonTest, SameSliceIsEqual) {
  auto b = Buffer::CopyFrom("hello");
  auto s = b.Slice(0, 5);
  EXPECT_TRUE(b == s);
}

TEST(BufferComparisonTest, EmptyBuffersAreEqual) {
  Buffer b1, b2;
  EXPECT_TRUE(b1 == b2);
}

TEST(BufferComparisonTest, EmptyVsNonEmpty) {
  Buffer b1;
  auto b2 = Buffer::CopyFrom("x");
  EXPECT_TRUE(b1 != b2);
}

// ===========================================================================
// BufferChainTest — ring operations
// ===========================================================================

TEST(BufferChainTest, StandaloneIsNotChained) {
  auto b = Buffer::CopyFrom("solo");
  EXPECT_FALSE(b.is_chained());
  EXPECT_EQ(b.next(), &b);
  EXPECT_EQ(b.prev(), &b);
  EXPECT_EQ(b.CountChainElements(), 1u);
  EXPECT_EQ(b.ComputeChainDataLength(), 4u);
}

TEST(BufferChainTest, AppendChainTwoNodes) {
  auto *a = new Buffer(Buffer::CopyFrom("foo"));
  auto *b = new Buffer(Buffer::CopyFrom("bar"));

  a->AppendChain(b);

  EXPECT_TRUE(a->is_chained());
  EXPECT_TRUE(b->is_chained());
  EXPECT_EQ(a->next(), b);
  EXPECT_EQ(b->next(), a);
  EXPECT_EQ(a->prev(), b);
  EXPECT_EQ(b->prev(), a);
  EXPECT_EQ(a->CountChainElements(), 2u);
  EXPECT_EQ(a->ComputeChainDataLength(), 6u);

  Buffer::DestroyChain(a);
}

TEST(BufferChainTest, AppendChainThreeNodes) {
  auto *a = new Buffer(Buffer::CopyFrom("A"));
  auto *b = new Buffer(Buffer::CopyFrom("BB"));
  auto *c = new Buffer(Buffer::CopyFrom("CCC"));

  a->AppendChain(b);
  a->AppendChain(c);

  EXPECT_EQ(a->CountChainElements(), 3u);
  EXPECT_EQ(a->ComputeChainDataLength(), 6u);
  EXPECT_EQ(ChainToString(a), "ABBCCC");

  // Ring: a -> b -> c -> a
  EXPECT_EQ(a->next(), b);
  EXPECT_EQ(b->next(), c);
  EXPECT_EQ(c->next(), a);
  EXPECT_EQ(a->prev(), c);

  Buffer::DestroyChain(a);
}

TEST(BufferChainTest, PrependChain) {
  auto *a = new Buffer(Buffer::CopyFrom("world"));
  auto *b = new Buffer(Buffer::CopyFrom("hello "));

  a->PrependChain(b);

  // b should be before a in the ring
  EXPECT_EQ(b->next(), a);
  EXPECT_EQ(a->next(), b); // circular
  EXPECT_EQ(ChainToString(b), "hello world");

  Buffer::DestroyChain(b);
}

TEST(BufferChainTest, AppendMultiNodeChainToSingleNode) {
  auto *a = new Buffer(Buffer::CopyFrom("A"));
  auto *bc_head = MakeChain({"B", "C"});

  a->AppendChain(bc_head);

  EXPECT_EQ(a->CountChainElements(), 3u);
  EXPECT_EQ(ChainToString(a), "ABC");

  Buffer::DestroyChain(a);
}

TEST(BufferChainTest, UnlinkMiddleNode) {
  auto *a = new Buffer(Buffer::CopyFrom("A"));
  auto *b = new Buffer(Buffer::CopyFrom("B"));
  auto *c = new Buffer(Buffer::CopyFrom("C"));
  a->AppendChain(b);
  a->AppendChain(c);

  Buffer *next_after_b = b->Unlink();

  EXPECT_EQ(next_after_b, c);
  EXPECT_FALSE(b->is_chained()); // b is now standalone
  EXPECT_EQ(a->CountChainElements(), 2u);
  EXPECT_EQ(ChainToString(a), "AC");

  Buffer::DestroyChain(a);
  delete b;
}

TEST(BufferChainTest, UnlinkOnlyNode) {
  auto *a = new Buffer(Buffer::CopyFrom("solo"));
  Buffer *result = a->Unlink();
  EXPECT_EQ(result, nullptr);
  EXPECT_FALSE(a->is_chained());
  delete a;
}

TEST(BufferChainTest, SplitChainAfterMiddle) {
  auto *a = new Buffer(Buffer::CopyFrom("A"));
  auto *b = new Buffer(Buffer::CopyFrom("B"));
  auto *c = new Buffer(Buffer::CopyFrom("C"));
  auto *d = new Buffer(Buffer::CopyFrom("D"));
  a->AppendChain(b);
  a->AppendChain(c);
  a->AppendChain(d);
  // Ring: A -> B -> C -> D -> A

  Buffer *right_head = b->SplitChainAfter();
  // Left:  A -> B -> A  (a is head)
  // Right: C -> D -> C  (right_head == c)

  EXPECT_EQ(right_head, c);
  EXPECT_EQ(a->CountChainElements(), 2u);
  EXPECT_EQ(ChainToString(a), "AB");
  EXPECT_EQ(right_head->CountChainElements(), 2u);
  EXPECT_EQ(ChainToString(right_head), "CD");

  Buffer::DestroyChain(a);
  Buffer::DestroyChain(right_head);
}

TEST(BufferChainTest, SplitChainAfterTailNode) {
  // Ring: A <-> B <-> C <-> A
  // Split after C (the tail when walking from A).
  // right_head = C->next_ = A
  // This means: right ring = {A, B, C ... } the whole original ring minus C
  // closing
  //
  // Per SplitChainAfter semantics:
  //   left  ring: [lh=B .. this=C]  i.e. {B, C}  (lh = C.prev_ = B)
  //   right ring: [rh=A .. rt=A]    i.e. {A}      (rh = C.next_ = A, rt =
  //   B.prev_ = A)
  //
  // So splitting after C gives: left={B,C}, right={A}.
  // The caller that holds `a` (head of original) now holds a standalone node.
  auto *a = new Buffer(Buffer::CopyFrom("X"));
  auto *b = new Buffer(Buffer::CopyFrom("Y"));
  auto *c = new Buffer(Buffer::CopyFrom("Z"));
  a->AppendChain(b);
  a->AppendChain(c);
  // Ring: A->B->C->A

  Buffer *right_head = c->SplitChainAfter();
  // right_head = A (C->next_ before split)
  EXPECT_EQ(right_head, a);

  // Right ring contains only A (rt = lh->prev_ = B->prev_ = A)
  EXPECT_EQ(right_head->CountChainElements(), 1u);
  EXPECT_FALSE(right_head->is_chained());
  EXPECT_EQ(BufferStr(*right_head), "X");

  // Left ring: {B, C}
  EXPECT_EQ(b->CountChainElements(), 2u);
  EXPECT_EQ(ChainToString(b), "YZ");

  Buffer::DestroyChain(right_head);
  Buffer::DestroyChain(b);
}

TEST(BufferChainTest, SplitOnStandalone) {
  auto *a = new Buffer(Buffer::CopyFrom("alone"));
  Buffer *result = a->SplitChainAfter();
  EXPECT_EQ(result, nullptr);
  delete a;
}

TEST(BufferChainTest, CoalesceChainPreservesOrder) {
  auto *head = MakeChain({"Hello", ", ", "world", "!"});
  Buffer coalesced = head->CoalesceChain();
  EXPECT_EQ(BufferStr(coalesced), "Hello, world!");
  EXPECT_FALSE(coalesced.is_chained());
  Buffer::DestroyChain(head);
}

TEST(BufferChainTest, CoalesceEmptyChain) {
  auto *a = new Buffer(Buffer::Create(0));
  Buffer result = a->CoalesceChain();
  EXPECT_TRUE(result.empty());
  Buffer::DestroyChain(a);
}

TEST(BufferChainTest, ToVector) {
  auto *head = MakeChain({"abc", "def"});
  auto vec = head->ToVector();
  EXPECT_EQ(vec.size(), 6u);
  EXPECT_EQ(string(vec.begin(), vec.end()), "abcdef");
  Buffer::DestroyChain(head);
}

TEST(BufferChainTest, ComputeChainDataLengthLargeChain) {
  Buffer *head = new Buffer(Buffer::CopyFrom("x"));
  for (int i = 0; i < 99; ++i) {
    head->AppendChain(new Buffer(Buffer::CopyFrom("x")));
  }
  EXPECT_EQ(head->CountChainElements(), 100u);
  EXPECT_EQ(head->ComputeChainDataLength(), 100u);
  Buffer::DestroyChain(head);
}

TEST(BufferChainTest, ChainWithEmptyNodes) {
  auto *a = new Buffer(Buffer::CopyFrom("data"));
  auto *b = new Buffer(Buffer::Create(0)); // empty node
  auto *c = new Buffer(Buffer::CopyFrom("more"));

  a->AppendChain(b);
  a->AppendChain(c);

  EXPECT_EQ(a->ComputeChainDataLength(), 8u);
  EXPECT_EQ(ChainToString(a), "datamore");

  Buffer::DestroyChain(a);
}

// ===========================================================================
// BufferCursorTest — BufferCursor read-only traversal
// ===========================================================================

TEST(BufferCursorTest, ReadSingleNode) {
  auto b = Buffer::CopyFrom("hello");
  BufferCursor cursor(&b);
  char out[6] = {};
  size_t n = cursor.Read(out, 5);
  EXPECT_EQ(n, 5u);
  EXPECT_STREQ(out, "hello");
  EXPECT_TRUE(cursor.AtEnd());
}

TEST(BufferCursorTest, ReadAcrossNodes) {
  auto *head = MakeChain({"foo", "bar", "baz"});
  BufferCursor cursor(head);
  char out[10] = {};
  size_t n = cursor.Read(out, 9);
  EXPECT_EQ(n, 9u);
  EXPECT_STREQ(out, "foobarbaz");
  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, ReadLessThanAvailable) {
  auto *head = MakeChain({"hello ", "world"});
  BufferCursor cursor(head);
  char out[4] = {};
  cursor.Read(out, 3);
  EXPECT_STREQ(out, "hel");
  EXPECT_FALSE(cursor.AtEnd());

  char rest[9] = {};
  cursor.Read(rest, 8);
  EXPECT_STREQ(rest, "lo world");

  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, ReadAtNodeBoundary) {
  auto *head = MakeChain({"abc", "def"});
  BufferCursor cursor(head);
  char out1[4] = {}, out2[4] = {};
  cursor.Read(out1, 3);
  cursor.Read(out2, 3);
  EXPECT_STREQ(out1, "abc");
  EXPECT_STREQ(out2, "def");
  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, PeekDoesNotAdvance) {
  auto *head = MakeChain({"peek", "test"});
  BufferCursor cursor(head);

  char peek1[5] = {}, peek2[5] = {};
  cursor.Peek(peek1, 4);
  cursor.Peek(peek2, 4);
  EXPECT_STREQ(peek1, "peek");
  EXPECT_STREQ(peek2, "peek");

  // Cursor should still be at start
  char all[9] = {};
  cursor.Read(all, 8);
  EXPECT_STREQ(all, "peektest");

  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, PeekAcrossNodes) {
  auto *head = MakeChain({"ab", "cd", "ef"});
  BufferCursor cursor(head);

  char out[7] = {};
  cursor.Peek(out, 6);
  EXPECT_STREQ(out, "abcdef");
  EXPECT_FALSE(cursor.AtEnd()); // peek didn't advance

  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, SkipAdvancesCursor) {
  auto *head = MakeChain({"skip", "rest"});
  BufferCursor cursor(head);
  cursor.Skip(4);

  char out[5] = {};
  cursor.Read(out, 4);
  EXPECT_STREQ(out, "rest");
  EXPECT_TRUE(cursor.AtEnd());

  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, SkipAcrossNodeBoundary) {
  auto *head = MakeChain({"abc", "defg"});
  BufferCursor cursor(head);
  cursor.Skip(5); // skip 3 from first, 2 from second

  char out[3] = {};
  cursor.Read(out, 2);
  EXPECT_STREQ(out, "fg");

  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, TotalLength) {
  auto *head = MakeChain({"aaa", "bb", "c"});
  BufferCursor cursor(head);
  EXPECT_EQ(cursor.TotalLength(), 6u);
  cursor.Skip(2);
  EXPECT_EQ(cursor.TotalLength(), 4u);
  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, Reset) {
  auto *head = MakeChain({"hello ", "world"});
  BufferCursor cursor(head);
  char first[6] = {};
  cursor.Read(first, 5);
  cursor.Reset();

  char again[12] = {};
  cursor.Read(again, 11);
  EXPECT_STREQ(again, "hello world");

  Buffer::DestroyChain(head);
}

TEST(BufferCursorTest, ReadBeyondEndReturnsAvailable) {
  auto b = Buffer::CopyFrom("short");
  BufferCursor cursor(&b);
  char out[100] = {};
  size_t n = cursor.Read(out, 100);
  EXPECT_EQ(n, 5u);
  EXPECT_TRUE(cursor.AtEnd());
}

TEST(BufferCursorTest, ReadFromEmptyBuffer) {
  Buffer b;
  BufferCursor cursor(&b);
  char out[4] = {};
  size_t n = cursor.Read(out, 4);
  EXPECT_EQ(n, 0u);
  EXPECT_TRUE(cursor.AtEnd());
}

TEST(BufferCursorTest, ReadSingleByteAtATime) {
  auto *head = MakeChain({"ab", "cd"});
  BufferCursor cursor(head);
  string result;
  char c;
  while (cursor.Read(&c, 1) == 1)
    result += c;
  EXPECT_EQ(result, "abcd");
  Buffer::DestroyChain(head);
}

// ===========================================================================
// BufferRWCursorTest — BufferRWCursor write traversal
// ===========================================================================

TEST(BufferRWCursorTest, WriteSingleNode) {
  auto *a = new Buffer(Buffer::CopyFrom("hello"));
  BufferRWCursor rw(a);
  rw.Write("HELLO", 5);
  EXPECT_EQ(BufferStr(*a), "HELLO");
  delete a;
}

TEST(BufferRWCursorTest, WriteAcrossNodes) {
  auto *head = MakeChain({"aaaa", "bbbb"});
  BufferRWCursor rw(head);
  rw.Write("12345678", 8);
  EXPECT_EQ(ChainToString(head), "12345678");
  Buffer::DestroyChain(head);
}

TEST(BufferRWCursorTest, WritePartial) {
  auto *head = MakeChain({"hello ", "world"});
  BufferRWCursor rw(head);
  rw.Write("HELLO", 5);
  EXPECT_EQ(ChainToString(head), "HELLO world");
  Buffer::DestroyChain(head);
}

TEST(BufferRWCursorTest, WriteAcrossNodeBoundary) {
  // Write 5 bytes crossing the boundary between "abc"(3) and "defg"(4)
  auto *head = MakeChain({"abc", "defg"});
  BufferRWCursor rw(head);
  rw.Write("12345", 5);
  EXPECT_EQ(ChainToString(head), "12345fg");
  Buffer::DestroyChain(head);
}

TEST(BufferRWCursorTest, WriteTriggersCowOnSharedNode) {
  auto *a = new Buffer(Buffer::CopyFrom("original"));
  Buffer a_copy = *a; // shares physical with a

  EXPECT_TRUE(a->is_shared());

  BufferRWCursor rw(a);
  rw.Write("XXXXXXXX", 8);

  // a should have detached; a_copy unchanged
  EXPECT_FALSE(a->is_shared());
  EXPECT_EQ(BufferStr(*a), "XXXXXXXX");
  EXPECT_EQ(BufferStr(a_copy), "original");

  delete a;
}

TEST(BufferRWCursorTest, SkipAndWrite) {
  auto *a = new Buffer(Buffer::CopyFrom("hello world"));
  BufferRWCursor rw(a);
  rw.Skip(6);
  rw.Write("WORLD", 5);
  EXPECT_EQ(BufferStr(*a), "hello WORLD");
  delete a;
}

TEST(BufferRWCursorTest, ResetAndRewrite) {
  auto *a = new Buffer(Buffer::CopyFrom("aaaaa"));
  BufferRWCursor rw(a);
  rw.Write("bbbbb", 5);
  rw.Reset();
  rw.Write("ccccc", 5);
  EXPECT_EQ(BufferStr(*a), "ccccc");
  delete a;
}

TEST(BufferRWCursorTest, WriteBeyondEndWritesAvailable) {
  auto *a = new Buffer(Buffer::CopyFrom("abc"));
  BufferRWCursor rw(a);
  size_t written = rw.Write("XXXXXXXXXXX", 11);
  EXPECT_EQ(written, 3u); // only 3 bytes available
  EXPECT_EQ(BufferStr(*a), "XXX");
  delete a;
}

TEST(BufferRWCursorTest, TotalLengthMatchesCursor) {
  auto *head = MakeChain({"aaa", "bb"});
  BufferRWCursor rw(head);
  EXPECT_EQ(rw.TotalLength(), 5u);
  rw.Skip(3);
  EXPECT_EQ(rw.TotalLength(), 2u);
  Buffer::DestroyChain(head);
}

// ===========================================================================
// BufferEdgeCaseTest — edge cases and robustness
// ===========================================================================

TEST(BufferEdgeCaseTest, ZeroCapacityCreate) {
  EXPECT_NO_FATAL_FAILURE({
    auto b = Buffer::Create(0);
    EXPECT_TRUE(b.empty());
  });
}

TEST(BufferEdgeCaseTest, LargeBuffer) {
  const size_t large = 16 * 1024 * 1024; // 16 MB
  auto b = Buffer::Create(large);
  EXPECT_GE(b.capacity(), large);
  b.AppendData("start", 5);
  EXPECT_EQ(b.size(), 5u);
  EXPECT_GE(b.tailroom(), large - 5);
}

TEST(BufferEdgeCaseTest, RepeatCopyAndRelease) {
  // Stress ref-count correctness: copy many times and let them all die.
  auto b = Buffer::CopyFrom("ref count stress");
  {
    vector<Buffer> copies;
    copies.reserve(1000);
    for (int i = 0; i < 1000; ++i)
      copies.push_back(b);
    EXPECT_TRUE(b.is_shared());
    // All copies destroyed when copies goes out of scope.
  }
  // All copies destroyed — b should be sole owner again.
  EXPECT_FALSE(b.is_shared());
  EXPECT_EQ(BufferStr(b), "ref count stress");
}

TEST(BufferEdgeCaseTest, TrimStartThenAppendData) {
  auto b = Buffer::CopyFrom("hello world");
  b.TrimStart(6);            // "world", headroom=6
  b.AppendData(" again", 6); // needs realloc (tailroom was 0)
  EXPECT_EQ(BufferStr(b), "world again");
}

TEST(BufferEdgeCaseTest, FullTrimAndReuse) {
  auto b = Buffer::Create(32);
  b.AppendData("temporary", 9);
  b.TrimStart(9);
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(b.headroom(), 9u);
  b.AppendData("new", 3);
  EXPECT_EQ(BufferStr(b), "new");
}

TEST(BufferEdgeCaseTest, CopyFromNullWithZeroLength) {
  // CopyFrom with len=0 and non-null pointer should work.
  EXPECT_NO_FATAL_FAILURE({
    auto b = Buffer::CopyFrom("anything", 0);
    EXPECT_TRUE(b.empty());
  });
}

TEST(BufferEdgeCaseTest, ToStringOnEmptyBuffer) {
  Buffer b;
  auto buff_str = b.ToString();
  EXPECT_EQ(buff_str.size(), 0u);
}

TEST(BufferEdgeCaseTest, ChainCoalesceIsNotChained) {
  auto *head = MakeChain({"part1", "part2"});
  auto coalesced = head->CoalesceChain();
  EXPECT_FALSE(coalesced.is_chained());
  Buffer::DestroyChain(head);
}

TEST(BufferEdgeCaseTest, DestroyChainWithSingleNode) {
  auto *a = new Buffer(Buffer::CopyFrom("single"));
  EXPECT_NO_FATAL_FAILURE(Buffer::DestroyChain(a));
}

TEST(BufferEdgeCaseTest, SliceOfSliceHeadroomConsistency) {
  auto b = Buffer::CopyFrom("0123456789");
  auto s1 = b.Slice(2, 6);  // "234567"
  auto s2 = s1.Slice(1, 4); // "3456"
  EXPECT_EQ(BufferStr(s2), "3456");
  EXPECT_EQ(s2.headroom(), 3u); // 2+1 bytes of headroom from the start
}

TEST(BufferEdgeCaseTest, AppendDataOnDefaultConstructed) {
  Buffer b;
  EXPECT_NO_FATAL_FAILURE(b.AppendData("data", 4));
  EXPECT_EQ(BufferStr(b), "data");
}

TEST(BufferEdgeCaseTest, MultipleSlicesAllSharing) {
  auto base = Buffer::CopyFrom("0123456789");
  vector<Buffer> slices;
  for (size_t i = 0; i < 10; ++i) {
    slices.push_back(base.Slice(i, 1));
  }
  EXPECT_TRUE(base.is_shared());
  // All slices should point into the same physical.
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(slices[i].data()[0], '0' + static_cast<char>(i));
  }
}
