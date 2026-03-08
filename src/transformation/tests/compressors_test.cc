// ---------------------------------------------------------------------------
// GTest suite for ICompressor implementations.
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "transformation/i_data_transformer.h"
#include "transformation/transformer_factory.h"

using namespace utils::transformation;

// ---------------------------------------------------------------------------
// Test data helpers
// ---------------------------------------------------------------------------
namespace {

ByteBuffer MakeCompressibleData(size_t size) {
  // Highly compressible: repeating pattern.
  ByteBuffer buf(size);
  for (size_t i = 0; i < size; ++i) {
    buf[i] = static_cast<uint8_t>(i % 37);
  }
  return buf;
}

ByteBuffer MakeRandomishData(size_t size) {
  // Low-entropy data using a simple LCG — predictable but not compressible.
  ByteBuffer buf(size);
  uint32_t state = 0xDEADBEEFu;
  for (auto &b : buf) {
    state = state * 1664525u + 1013904223u;
    b = static_cast<uint8_t>(state >> 24);
  }
  return buf;
}

} // namespace

// ---------------------------------------------------------------------------
// CompressorFixture
//
// Parameterised over CompressorType so every test runs on each compressor.
// ---------------------------------------------------------------------------
class CompressorFixture : public ::testing::TestWithParam<CompressorType> {
protected:
  void SetUp() override {
    compressor_ = TransformerFactory::CreateCompressor(GetParam());
    if (!compressor_) {
      GTEST_SKIP() << "Compressor not compiled in for this build.";
    }
  }

  std::unique_ptr<ICompressor> compressor_;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_P(CompressorFixture, NameIsNonEmpty) {
  EXPECT_FALSE(compressor_->name().empty());
}

TEST_P(CompressorFixture, LibraryVersionIsNonEmpty) {
  EXPECT_FALSE(compressor_->library_version().empty());
}

TEST_P(CompressorFixture, CategoryIsCompression) {
  EXPECT_EQ(compressor_->category(), TransformCategory::kCompression);
}

TEST_P(CompressorFixture, MaxCompressedSizeIsPositive) {
  EXPECT_GT(compressor_->MaxCompressedSize(1024), 0u);
}

TEST_P(CompressorFixture, RoundTripSmallBuffer) {
  const std::string kOriginal = "Hello, datatransform!";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kOriginal.data()),
               kOriginal.size());

  ByteBuffer compressed;
  ASSERT_TRUE(compressor_->Compress(src, compressed));
  EXPECT_GT(compressed.size(), 0u);

  ByteBuffer decompressed;
  ASSERT_TRUE(
      compressor_->Decompress(compressed, decompressed, kOriginal.size()));

  ASSERT_EQ(decompressed.size(), kOriginal.size());
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(decompressed.data()),
                        decompressed.size()),
            kOriginal);
}

TEST_P(CompressorFixture, RoundTripCompressibleData_1MB) {
  const ByteBuffer original = MakeCompressibleData(1024 * 1024);
  ByteSpan src(original.data(), original.size());

  ByteBuffer compressed;
  ASSERT_TRUE(compressor_->Compress(src, compressed));

  // Compressible data should shrink.
  EXPECT_LT(compressed.size(), original.size());

  ByteBuffer decompressed;
  ASSERT_TRUE(
      compressor_->Decompress(compressed, decompressed, original.size()));

  EXPECT_EQ(original, decompressed);
}

TEST_P(CompressorFixture, RoundTripIncompressibleData_64KB) {
  const ByteBuffer original = MakeRandomishData(64 * 1024);
  ByteSpan src(original.data(), original.size());

  ByteBuffer compressed;
  ASSERT_TRUE(compressor_->Compress(src, compressed));

  ByteBuffer decompressed;
  ASSERT_TRUE(
      compressor_->Decompress(compressed, decompressed, original.size()));

  EXPECT_EQ(original, decompressed);
}

TEST_P(CompressorFixture, RoundTripEmptyInput) {
  ByteBuffer compressed;
  ASSERT_TRUE(compressor_->Compress(ByteSpan{}, compressed));

  ByteBuffer decompressed;
  ASSERT_TRUE(compressor_->Decompress(compressed, decompressed, 0));

  EXPECT_TRUE(decompressed.empty());
}

TEST_P(CompressorFixture, CompressionLevelGetSet) {
  // Snappy has no levels — just ensure the interface is callable.
  const int original_level = compressor_->compression_level();
  compressor_->set_compression_level(original_level);
  EXPECT_EQ(compressor_->compression_level(), original_level);
}

// ---------------------------------------------------------------------------
// Instantiate for all available compressor types
// ---------------------------------------------------------------------------
INSTANTIATE_TEST_SUITE_P(
    AllCompressors, CompressorFixture,
    ::testing::Values(CompressorType::kLZ4, CompressorType::kZstd,
                      CompressorType::kSnappy),
    [](const ::testing::TestParamInfo<CompressorType> &info) {
      switch (info.param) {
      case CompressorType::kLZ4:
        return "LZ4";
      case CompressorType::kZstd:
        return "Zstd";
      case CompressorType::kSnappy:
        return "Snappy";
      }
      return "Unknown";
    });

// ---------------------------------------------------------------------------
// Zstd-specific tests (compression level range)
// ---------------------------------------------------------------------------
#if defined(DATATRANSFORM_HAS_ZSTD)
TEST(ZstdSpecific, CompressionLevels_1_Through_9) {
  for (int level = 1; level <= 9; ++level) {
    auto c = TransformerFactory::CreateCompressor(CompressorType::kZstd, level);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->compression_level(), level);

    const ByteBuffer original = MakeCompressibleData(512 * 1024);
    ByteBuffer compressed, decompressed;
    ASSERT_TRUE(
        c->Compress(ByteSpan(original.data(), original.size()), compressed));
    ASSERT_TRUE(c->Decompress(ByteSpan(compressed.data(), compressed.size()),
                              decompressed, original.size()));
    EXPECT_EQ(original, decompressed);
  }
}
#endif
