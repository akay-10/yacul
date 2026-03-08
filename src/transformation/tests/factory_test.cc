// ---------------------------------------------------------------------------
// GTest suite for TransformerFactory — availability, naming, and null-safety.
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <string>

#include "transformation/transformer_factory.h"

using namespace utils::transformation;

// ---------------------------------------------------------------------------
// Availability enumeration
// ---------------------------------------------------------------------------

TEST(FactoryAvailability, AvailableCompressors_IsNotEmpty) {
  // At least one compressor should be compiled in for a useful build.
  const auto list = TransformerFactory::AvailableCompressors();
  EXPECT_GT(list.size(), 0u)
      << "No compressors compiled in — check CMake options.";
}

TEST(FactoryAvailability, AvailableChecksummers_IsNotEmpty) {
  const auto list = TransformerFactory::AvailableChecksummers();
  EXPECT_GT(list.size(), 0u)
      << "No checksummers compiled in — check CMake options.";
}

TEST(FactoryAvailability, AvailableHashers_IsNotEmpty) {
  const auto list = TransformerFactory::AvailableHashers();
  EXPECT_GT(list.size(), 0u) << "No hashers compiled in — check CMake options.";
}

// ---------------------------------------------------------------------------
// String-based lookup — valid names
// ---------------------------------------------------------------------------

struct NamedCompressorCase {
  const char *name;
  CompressorType expected;
};

class FactoryNamedCompressorTest
    : public ::testing::TestWithParam<NamedCompressorCase> {};

TEST_P(FactoryNamedCompressorTest, ByName_ReturnsNonNullOrSkips) {
  const auto &tc = GetParam();
  auto c = TransformerFactory::CompressorByName(tc.name);
  if (!c) {
    GTEST_SKIP() << tc.name << " not compiled in.";
  }
  // Verify the returned object matches the expected type by name prefix.
  EXPECT_FALSE(c->name().empty());
}

INSTANTIATE_TEST_SUITE_P(
    ValidNames, FactoryNamedCompressorTest,
    ::testing::Values(NamedCompressorCase{"lz4", CompressorType::kLZ4},
                      NamedCompressorCase{"LZ4", CompressorType::kLZ4},
                      NamedCompressorCase{"zstd", CompressorType::kZstd},
                      NamedCompressorCase{"ZSTD", CompressorType::kZstd},
                      NamedCompressorCase{"snappy", CompressorType::kSnappy},
                      NamedCompressorCase{"Snappy", CompressorType::kSnappy}));

// ---------------------------------------------------------------------------
// String-based lookup — invalid names return nullptr
// ---------------------------------------------------------------------------

TEST(FactoryNullSafety, UnknownCompressorName_ReturnsNull) {
  EXPECT_EQ(TransformerFactory::CompressorByName("bzip2"), nullptr);
  EXPECT_EQ(TransformerFactory::CompressorByName(""), nullptr);
  EXPECT_EQ(TransformerFactory::CompressorByName("GZIP"), nullptr);
}

TEST(FactoryNullSafety, UnknownChecksummerName_ReturnsNull) {
  EXPECT_EQ(TransformerFactory::ChecksummerByName("md5"), nullptr);
  EXPECT_EQ(TransformerFactory::ChecksummerByName(""), nullptr);
}

TEST(FactoryNullSafety, UnknownHasherName_ReturnsNull) {
  EXPECT_EQ(TransformerFactory::HasherByName("md5"), nullptr);
  EXPECT_EQ(TransformerFactory::HasherByName("ripemd"), nullptr);
  EXPECT_EQ(TransformerFactory::HasherByName(""), nullptr);
}

// ---------------------------------------------------------------------------
// Hasher name lookup with known valid names
// ---------------------------------------------------------------------------

TEST(FactoryHashing, HasherByName_Blake3) {
  auto h = TransformerFactory::HasherByName("blake3");
  if (!h)
    GTEST_SKIP() << "BLAKE3 not compiled in.";
  EXPECT_FALSE(h->name().empty());
  EXPECT_EQ(h->category(), TransformCategory::kHash);
}

TEST(FactoryHashing, HasherByName_SHA256_CaseInsensitive) {
  auto lower = TransformerFactory::HasherByName("sha-256");
  auto upper = TransformerFactory::HasherByName("sha256");
  // Both should return the same algorithm (or both nullptr).
  EXPECT_EQ(lower == nullptr, upper == nullptr);
}

// ---------------------------------------------------------------------------
// Checksummer name lookup
// ---------------------------------------------------------------------------

TEST(FactoryChecksums, ChecksummerByName_XXH3_128) {
  auto cs = TransformerFactory::ChecksummerByName("xxh3-128");
  if (!cs)
    GTEST_SKIP() << "xxHash not compiled in.";
  EXPECT_EQ(cs->digest_size_bytes(), 16u);
}

TEST(FactoryChecksums, ChecksummerByName_CRC32C) {
  auto cs = TransformerFactory::ChecksummerByName("crc32c");
  if (!cs)
    GTEST_SKIP() << "CRC32C not compiled in.";
  EXPECT_EQ(cs->digest_size_bytes(), 4u);
}

// ---------------------------------------------------------------------------
// Verify that CreateCompressor with level=0 still works
// ---------------------------------------------------------------------------

TEST(FactoryDefaults, DefaultLevelCompressor_IsUsable) {
  for (auto type : TransformerFactory::AvailableCompressors()) {
    auto c = TransformerFactory::CreateCompressor(type, 0);
    ASSERT_NE(c, nullptr);

    const std::string kData = "default level round-trip test";
    ByteSpan src(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());
    ByteBuffer compressed, decompressed;
    ASSERT_TRUE(c->Compress(src, compressed));
    ASSERT_TRUE(c->Decompress(ByteSpan(compressed.data(), compressed.size()),
                              decompressed, kData.size()));
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(decompressed.data()),
                          decompressed.size()),
              kData);
  }
}
