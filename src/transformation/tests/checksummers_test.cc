// ---------------------------------------------------------------------------
// GTest suite for IChecksummer implementations.
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "transformation/i_data_transformer.h"
#include "transformation/transformer_factory.h"

using namespace utils::transformation;

// ---------------------------------------------------------------------------
// ChecksummerFixture — parameterised over ChecksumType
// ---------------------------------------------------------------------------
class ChecksummerFixture : public ::testing::TestWithParam<ChecksumType> {
protected:
  void SetUp() override {
    checksummer_ = TransformerFactory::CreateChecksummer(GetParam());
    if (!checksummer_) {
      GTEST_SKIP() << "Checksummer not compiled in for this build.";
    }
  }

  std::unique_ptr<IChecksummer> checksummer_;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_P(ChecksummerFixture, NameIsNonEmpty) {
  EXPECT_FALSE(checksummer_->name().empty());
}

TEST_P(ChecksummerFixture, LibraryVersionIsNonEmpty) {
  EXPECT_FALSE(checksummer_->library_version().empty());
}

TEST_P(ChecksummerFixture, CategoryIsChecksum) {
  EXPECT_EQ(checksummer_->category(), TransformCategory::kChecksum);
}

TEST_P(ChecksummerFixture, DigestSizeIsPositive) {
  EXPECT_GT(checksummer_->digest_size_bytes(), 0u);
}

TEST_P(ChecksummerFixture, OneShot_OutputSizeMatchesDigestSize) {
  const std::string kData = "The quick brown fox jumps over the lazy dog";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  ByteBuffer result = checksummer_->Compute(src);
  EXPECT_EQ(result.size(), checksummer_->digest_size_bytes());
}

TEST_P(ChecksummerFixture, OneShot_HexLengthIsDoubleDigestSize) {
  const std::string kData = "test data";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  std::string hex = checksummer_->ComputeHex(src);
  EXPECT_EQ(hex.size(), checksummer_->digest_size_bytes() * 2);
  // Verify it's lowercase hex.
  for (char c : hex) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
        << "Non-hex char: " << c;
  }
}

TEST_P(ChecksummerFixture, Determinism_SameInputProducesSameOutput) {
  const std::string kData = "deterministic test";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  EXPECT_EQ(checksummer_->Compute(src), checksummer_->Compute(src));
  EXPECT_EQ(checksummer_->ComputeHex(src), checksummer_->ComputeHex(src));
}

TEST_P(ChecksummerFixture, DifferentInputsProduceDifferentOutputs) {
  const std::string kA = "input A";
  const std::string kB = "input B";
  ByteSpan a(reinterpret_cast<const uint8_t *>(kA.data()), kA.size());
  ByteSpan b(reinterpret_cast<const uint8_t *>(kB.data()), kB.size());

  EXPECT_NE(checksummer_->Compute(a), checksummer_->Compute(b));
}

TEST_P(ChecksummerFixture, Streaming_MatchesOneShot) {
  const std::string kData = "streaming vs one-shot consistency test payload";
  ByteSpan full(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  // One-shot reference.
  ByteBuffer one_shot = checksummer_->Compute(full);

  // Streaming — split into three chunks.
  checksummer_->Reset();
  const size_t chunk = kData.size() / 3;
  checksummer_->Update(
      ByteSpan(reinterpret_cast<const uint8_t *>(kData.data()), chunk));
  checksummer_->Update(
      ByteSpan(reinterpret_cast<const uint8_t *>(kData.data() + chunk), chunk));
  checksummer_->Update(
      ByteSpan(reinterpret_cast<const uint8_t *>(kData.data() + 2 * chunk),
               kData.size() - 2 * chunk));
  ByteBuffer streaming = checksummer_->Finalize();

  EXPECT_EQ(one_shot, streaming);
}

TEST_P(ChecksummerFixture, Streaming_ResetClearsState) {
  const std::string kA = "first";
  const std::string kB = "second";

  checksummer_->Reset();
  checksummer_->Update(
      ByteSpan(reinterpret_cast<const uint8_t *>(kA.data()), kA.size()));
  checksummer_->Reset(); // <-- discard first chunk
  checksummer_->Update(
      ByteSpan(reinterpret_cast<const uint8_t *>(kB.data()), kB.size()));
  ByteBuffer after_reset = checksummer_->Finalize();

  // Should match a fresh one-shot over kB only.
  ByteBuffer one_shot = checksummer_->Compute(
      ByteSpan(reinterpret_cast<const uint8_t *>(kB.data()), kB.size()));

  EXPECT_EQ(after_reset, one_shot);
}

TEST_P(ChecksummerFixture, EmptyInput_ReturnsFixedSizeResult) {
  ByteBuffer result = checksummer_->Compute(ByteSpan{});
  EXPECT_EQ(result.size(), checksummer_->digest_size_bytes());
}

// ---------------------------------------------------------------------------
// Instantiate for all checksum types
// ---------------------------------------------------------------------------
INSTANTIATE_TEST_SUITE_P(
    AllChecksummers, ChecksummerFixture,
    ::testing::Values(ChecksumType::kXXH32, ChecksumType::kXXH64,
                      ChecksumType::kXXH3_64, ChecksumType::kXXH3_128,
                      ChecksumType::kCRC32C),
    [](const ::testing::TestParamInfo<ChecksumType> &info) {
      switch (info.param) {
      case ChecksumType::kXXH32:
        return "XXH32";
      case ChecksumType::kXXH64:
        return "XXH64";
      case ChecksumType::kXXH3_64:
        return "XXH3_64";
      case ChecksumType::kXXH3_128:
        return "XXH3_128";
      case ChecksumType::kCRC32C:
        return "CRC32C";
      }
      return "Unknown";
    });

// ---------------------------------------------------------------------------
// Known-value tests (CRC32C)
// These pin against externally-verified reference values.
// ---------------------------------------------------------------------------
#if defined(UTILS_TRANSFORMATION_HAS_CRC32C)
class Crc32cKnownValuesTest : public ::testing::Test {
protected:
  void SetUp() override {
    cs_ = TransformerFactory::CreateChecksummer(ChecksumType::kCRC32C);
    ASSERT_NE(cs_, nullptr);
  }
  std::unique_ptr<IChecksummer> cs_;
};

TEST_F(Crc32cKnownValuesTest, EmptyString_IsZero) {
  auto result = cs_->Compute(ByteSpan{});
  // CRC32C of empty data is 0x00000000.
  uint32_t crc = 0;
  for (size_t i = 0; i < result.size(); ++i) {
    crc |= static_cast<uint32_t>(result[i]) << (i * 8);
  }
  EXPECT_EQ(crc, 0u);
}

TEST_F(Crc32cKnownValuesTest, KnownString_Matches_ReferenceValue) {
  // "123456789" -> CRC32C = 0xE3069283 (little-endian in buffer)
  const std::string kInput = "123456789";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kInput.data()), kInput.size());
  auto result = cs_->Compute(src);
  ASSERT_EQ(result.size(), 4u);
  uint32_t crc = 0;
  for (size_t i = 0; i < result.size(); ++i) {
    crc |= static_cast<uint32_t>(result[i]) << (i * 8);
  }
  EXPECT_EQ(crc, 0xE3069283u);
}
#endif
