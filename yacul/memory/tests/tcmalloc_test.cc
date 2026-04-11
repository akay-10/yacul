#include <gperftools/malloc_extension.h>
#include <gtest/gtest.h>

void DumpSizeClassStats(const std::string &label) {
  char buf[1 << 14 /* 16KB */];
  MallocExtension::instance()->GetStats(buf, sizeof(buf));
  std::cout << "\n=== " << label << " ===\n" << buf << "\n";
}

TEST(TCMaclloTest, SizeClassStats) {
  DumpSizeClassStats("Before allocaing 1k 64byte allocations");
  size_t allocated = 0;
  MallocExtension::instance()->GetNumericProperty(
    "generic.current_allocated_bytes", &allocated);

  void *arr_p[1000];
  for (int i = 0; i < 1000; i++) {
    arr_p[i] = malloc(64);
  }

  DumpSizeClassStats("After allocating");
  size_t new_allocated = 0;
  MallocExtension::instance()->GetNumericProperty(
    "generic.current_allocated_bytes", &new_allocated);

  EXPECT_EQ(64 * 1000, new_allocated - allocated);

  for (int i = 0; i < 1000; i++) {
    free(arr_p[i]);
  }

  DumpSizeClassStats("After freeing");
}
