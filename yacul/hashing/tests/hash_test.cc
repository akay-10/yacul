#include "yacul/hashing/hash.h"

#include "gtest/gtest.h"

using namespace utils::hashing;
using namespace std;

// Create a wrapper to turn the enum Value into a type
template <HashType H> struct HashTypeTag {
  static constexpr HashType value = H;
};

// Define the list of types to test
using HashTypesToTest =
  ::testing::Types<HashTypeTag<HashType::kStdHash>,
                   HashTypeTag<HashType::kMurmurHashx86_32>,
                   HashTypeTag<HashType::kAbseilHash>>;

// Define the Test Fixture
template <typename T> class HashTest : public ::testing::Test {
protected:
  // Helper to extract the enum value from the tag type
  static constexpr HashType GetHashType() { return T::value; }
};

TYPED_TEST_SUITE(HashTest, HashTypesToTest);

TYPED_TEST(HashTest, SanityTest) {
  constexpr HashType hash_type = TypeParam::value;
  // Just check that we don't crash.
  {
    Hash<string, hash_type> hasher;
    string str = "Hello World";
    const string const_str = "Hmm";
    hasher(str);
    hasher(const_str);
  }
  {
    Hash<const string, hash_type> hasher;
    string str = "Hello World";
    const string const_str = "Hmm";
    hasher(str);
    hasher(const_str);
  }
  {
    Hash<int, hash_type> hasher;
    int num = 31;
    const int const_num = 97;
    hasher(num);
    hasher(const_num);
  }
  {
    Hash<const int, hash_type> hasher;
    int num = 31;
    const int const_num = 97;
    hasher(num);
    hasher(const_num);
  }

  // hashing::Hash is a compile-time template class, the following code needs to
  // be wrapped in a conditional constexpr to avoid compiler to compile
  // unsupported code paths.
  if constexpr (hash_type != HashType::kStdHash) {
    // std::hash doesn't supports std::pair and std::tuple
    {
      Hash<pair<int, const string>, hash_type> hasher;
      pair<int, const string> p = {31, "Hello"};
      const pair<int, const string> q = {31, "Hello"};
      hasher(p);
      hasher(q);
    }
    {
      Hash<const pair<int, const string>, hash_type> hasher;
      pair<int, const string> p = {31, "Hello"};
      const pair<int, const string> q = {31, "Hello"};
      hasher(p);
      hasher(q);
    }
    {
      Hash<tuple<const int, int, string, const string>, hash_type> hasher;
      tuple<const int, int, string, const string> t = {31, 45, "Hello",
                                                       "World"};
      const tuple<const int, int, string, const string> u = {31, 45, "Hello",
                                                             "World"};
      hasher(t);
      hasher(u);
    }
    {
      Hash<const tuple<const int, int, string, const string>, hash_type> hasher;
      tuple<const int, int, string, const string> t = {31, 45, "Hello",
                                                       "World"};
      const tuple<const int, int, string, const string> u = {31, 45, "Hello",
                                                             "World"};
      hasher(t);
      hasher(u);
    }
  }
}

TYPED_TEST(HashTest, CollisionTest) {
  constexpr HashType hash_type = TypeParam::value;
  Hash<string, hash_type> str_hasher;
  string str1 = "test1";
  string str2 = "test2";
  // Ideally, hash of any random T to be 0 is 1 in 2^32.
  EXPECT_NE(str_hasher(str1), 0);
  // Same strings should produce same hash
  EXPECT_EQ(str_hasher(str1), str_hasher(str1));
  // Different strings should (likely) produce different hashes
  EXPECT_NE(str_hasher(str1), str_hasher(str2));
  const char *c_str1 = "test1";
  // String and c string with same content should produce same hash
  EXPECT_EQ(str_hasher(str1), str_hasher(c_str1));
  // Empty strings should hash consistently
  EXPECT_EQ(str_hasher(""), str_hasher(""));

  Hash<int, hash_type> int_hasher;
  int int1 = 37;
  int int2 = 97;
  // Ideally, hash of any random T to be 0 is 1 in 2^32.
  EXPECT_NE(int_hasher(int1), 0);
  // Same integers should produce same hash
  EXPECT_EQ(int_hasher(int1), int_hasher(int1));
  // Different integers should produce different hashes
  EXPECT_NE(int_hasher(int1), int_hasher(int2));

  if constexpr (hash_type != HashType::kStdHash) {
    // std::hash doesn't supports std::pair and std::tuple

    Hash<pair<int, int>, hash_type> pair_hasher;
    pair<int, int> pair1 = {37, 45};
    pair<int, int> pair2 = {97, 45};
    pair<int, int> pair3 = {45, 97}; // Different order
    // Ideally, hash of any random T to be 0 is 1 in 2^32.
    EXPECT_NE(pair_hasher(pair1), 0);
    // Same pairs should produce same hash
    EXPECT_EQ(pair_hasher(pair1), pair_hasher(pair1));
    // Different pairs should produce different hashes
    EXPECT_NE(pair_hasher(pair1), pair_hasher(pair2));
    EXPECT_NE(pair_hasher(pair1), pair_hasher(pair3));
    EXPECT_NE(pair_hasher(pair2), pair_hasher(pair3));
    // Intermediate seeds should be hashes of the last element
    EXPECT_NE(pair_hasher(pair1), int_hasher(45));
    if (hash_type == HashType::kMurmurHashx86_32) {
      // For MurmurHash, the final hash should be the hash of the second element
      // with the first element's hash as seed.
      EXPECT_EQ(pair_hasher(pair1), int_hasher(45, int_hasher(37)));
    }

    Hash<pair<int, string>, hash_type> pair_mixed_hasher;
    pair<int, string> pair_mixed1 = {37, "noob"};
    pair<int, string> pair_mixed2 = {97, "pro"};
    pair<int, string> pair_mixed3 = {37, "pro"};
    // Same pairs should produce same hash
    EXPECT_EQ(pair_mixed_hasher(pair_mixed1), pair_mixed_hasher(pair_mixed1));
    // Different pairs should produce different hashes
    EXPECT_NE(pair_mixed_hasher(pair_mixed1), pair_mixed_hasher(pair_mixed2));
    EXPECT_NE(pair_mixed_hasher(pair_mixed1), pair_mixed_hasher(pair_mixed3));
    EXPECT_NE(pair_mixed_hasher(pair_mixed2), pair_mixed_hasher(pair_mixed3));

    Hash<tuple<int, string, int>, hash_type> tuple_hasher;
    tuple<int, string, int> tuple1 = {37, "noob", 45};
    tuple<int, string, int> tuple2 = {97, "pro", 218};
    tuple<int, string, int> tuple3 = {37, "noob", 218};
    // Ideally, hash of any random T to be 0 is 1 in 2^32.
    EXPECT_NE(tuple_hasher(tuple1), 0);
    // Same tuples should produce same hash
    EXPECT_EQ(tuple_hasher(tuple1), tuple_hasher(tuple1));
    // Different tuples should produce different hashes
    EXPECT_NE(tuple_hasher(tuple1), tuple_hasher(tuple2));
    EXPECT_NE(tuple_hasher(tuple1), tuple_hasher(tuple3));
    EXPECT_NE(tuple_hasher(tuple2), tuple_hasher(tuple3));
  }

  // Expect low collision rate
  const int num_values = 1e6;
  unordered_set<uint32_t> seen_hashes;
  for (int i = 0; i < num_values; ++i) {
    uint32_t hash = int_hasher(i);
    seen_hashes.insert(hash);
  }
  // Allow up to 1% collisions
  EXPECT_GE(seen_hashes.size(), num_values * 0.99);
}

TYPED_TEST(HashTest, DistributionQualityTest) {
  constexpr HashType hash_type = TypeParam::value;
  Hash<int, hash_type> hasher;
  const int num_values = 1e6;
  const int num_buckets = 100;
  std::vector<int> buckets(num_buckets, 0);
  for (int i = 0; i < num_values; ++i) {
    size_t hash = hasher(i);
    buckets[hash % num_buckets]++;
  }
  // Check distribution - each bucket should have roughly
  // num_values/num_buckets hashes
  int expected = num_values / num_buckets;
  int min_acceptable = expected / 2; // Allow 50% deviation
  int max_acceptable = expected * 2;

  for (int count : buckets) {
    EXPECT_GE(count, min_acceptable);
    EXPECT_LE(count, max_acceptable);
  }
}
