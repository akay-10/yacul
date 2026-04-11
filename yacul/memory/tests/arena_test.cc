#include "yacul/memory/allocator.h"
#include "yacul/memory/arena.h"

#include <gtest/gtest.h>

using namespace std;
using namespace utils::memory;

struct Vec3 {
  float x, y, z;
};

struct Node {
  int value;
  Node *left = nullptr;
  Node *right = nullptr;
};

TEST(ArenaTest, BasicAllocation) {
  Arena arena(1024);
  int *i = arena.Alloc<int>(42);
  float *f = arena.Alloc<float>(3.14f);
  Vec3 *v = arena.Alloc<Vec3>(Vec3{1.f, 2.f, 3.f});

  EXPECT_EQ(*i, 42);
  EXPECT_EQ(*f, 3.14f);
  EXPECT_EQ(v->z, 3.f);

  cout << LOGVARS(*i, *f, v->x, v->y, v->z) << endl;
  cout << LOGVARS(arena.block_count(), arena.reserved_bytes(),
                  arena.total_allocated())
       << endl;
}

TEST(ArenaTest, Alignment) {
  Arena arena(4096);
  auto CheckAlign = [this](void *ptr, size_t align, const char *tag) {
    bool ok = (reinterpret_cast<uintptr_t>(ptr) % align) == 0;
    cout << tag << " aligned to " << align << ": " << (ok ? "OK" : "FAIL")
         << '\n';
    EXPECT_TRUE(ok);
  };

  CheckAlign(arena.AllocBytes(1, 1), 1, "1-byte");
  CheckAlign(arena.AllocBytes(3, 4), 4, "4-byte");
  CheckAlign(arena.AllocBytes(7, 16), 16, "16-byte");
  CheckAlign(arena.AllocBytes(1, 64), 64, "64-byte");
  CheckAlign(arena.AllocBytes(1, 256), 256, "256-byte");
  CheckAlign(arena.AllocBytes(1, 4096), 4096, "4096-byte");
}

TEST(ArenaTest, Growth) {
  Arena arena(64);
  constexpr int N = 1000;
  int *ptrs[N];
  for (int k = 0; k < N; ++k) {
    ptrs[k] = arena.Alloc<int>(k);
  }

  for (int k = 0; k < N; ++k)
    EXPECT_EQ(*ptrs[k], k);

  cout << "Allocated " << N << " ints across " << arena.block_count()
       << " block(s)\n";
  cout << LOGVARS(arena.reserved_bytes(), arena.total_allocated()) << '\n';
}

TEST(ArenaTest, Reset) {
  Arena arena(1024);
  for (int k = 0; k < 100; ++k)
    arena.Alloc<int>(k);

  cout << "Before reset: "
       << LOGVARS(arena.block_count(), arena.total_allocated()) << '\n';

  arena.Reset();

  cout << "After  reset: "
       << LOGVARS(arena.block_count(), arena.total_allocated()) << '\n';

  EXPECT_EQ(arena.block_count(), 1);
  EXPECT_EQ(arena.total_allocated(), 0);

  // Re-use after reset.
  int *p = arena.Alloc<int>(7);
  EXPECT_EQ(*p, 7);
}

TEST(ArenaTest, MarkRewind) {
  Arena arena(4096);
  int *permanent = arena.Alloc<int>(99);

  {
    ArenaMark mk = arena.Mark();

    int *tmp1 = arena.Alloc<int>(1);
    int *tmp2 = arena.Alloc<int>(2);
    (void)tmp1;
    (void)tmp2;

    size_t allocated_inside = arena.total_allocated();
    cout << "Inside mark: " << LOGVARS(allocated_inside, arena.block_count())
         << '\n';
    // mk goes out of scope → rewind
  }

  cout << "After  mark: "
       << LOGVARS(arena.total_allocated(), arena.block_count()) << '\n';

  // permanent pointer is still valid
  EXPECT_EQ(*permanent, 99);
  cout << "Permanent value still valid: " << *permanent << '\n';
}

TEST(ArenaTest, ArrayAllocation) {
  Arena arena(8192);
  constexpr size_t N = 256;
  float *buf = arena.AllocArray<float>(N);
  for (size_t i = 0; i < N; ++i)
    buf[i] = static_cast<float>(i) * 0.5f;

  EXPECT_EQ(buf[255], 127.5f);
  cout << LOGVARS(buf[0], buf[255]) << '\n';
}

TEST(ArenaTest, STLAdaptor) {
  Arena arena(64 * 1024);
  // std::vector backed by arena
  using IntVec = vector<int, ArenaAllocator<int>>;
  IntVec v(ArenaAllocator<int>{arena});
  for (int k = 0; k < 1000; ++k)
    v.push_back(k);

  EXPECT_EQ(v.size(), 1000);
  EXPECT_EQ(v[999], 999);
  cout << LOGVARS(v.size(), v.front(), v.back()) << '\n';

  // std::basic_string backed by arena
  using AStr = basic_string<char, char_traits<char>, ArenaAllocator<char>>;
  AStr s(ArenaAllocator<char>{arena});
  s = "Hello from arena-backed string!";
  cout << "String=" << s.c_str() << '\n';

  cout << LOGVARS(arena.reserved_bytes(), arena.block_count()) << '\n';
}

TEST(ArenaTest, TreeInArena) {
  Arena arena(16 * 1024);
  // Build a simple BST
  auto Insert = [&](auto &self, Node *&root, int val) -> void {
    if (!root) {
      root = arena.Alloc<Node>();
      root->value = val;
      return;
    }
    if (val < root->value)
      self(self, root->left, val);
    else
      self(self, root->right, val);
  };

  auto Sum = [](auto &self, const Node *n) -> long {
    if (!n)
      return 0;
    return n->value + self(self, n->left) + self(self, n->right);
  };

  Node *root = nullptr;
  const int values[] = {5, 3, 7, 1, 4, 6, 8, 2, 9, 0};
  for (int v : values)
    Insert(Insert, root, v);

  long s = Sum(Sum, root);
  cout << "Sum of {0..9} in BST=" << s << " (expected 45)\n";
  EXPECT_EQ(s, 45);
  cout << LOGVARS(arena.block_count(), arena.total_allocated()) << '\n';
}
