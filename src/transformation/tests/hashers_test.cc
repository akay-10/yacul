// ---------------------------------------------------------------------------
// GTest suite for IHasher implementations.
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
// HasherFixture — parameterised over HashType
// ---------------------------------------------------------------------------
class HasherFixture : public ::testing::TestWithParam<HashType> {
protected:
  void SetUp() override {
    hasher_ = TransformerFactory::CreateHasher(GetParam());
    if (!hasher_) {
      GTEST_SKIP() << "Hasher not compiled in for this build.";
    }
  }

  std::unique_ptr<IHasher> hasher_;
};

// ---------------------------------------------------------------------------
// Common tests — all hashers
// ---------------------------------------------------------------------------

TEST_P(HasherFixture, NameIsNonEmpty) { EXPECT_FALSE(hasher_->name().empty()); }

TEST_P(HasherFixture, LibraryVersionIsNonEmpty) {
  EXPECT_FALSE(hasher_->library_version().empty());
}

TEST_P(HasherFixture, CategoryIsHash) {
  EXPECT_EQ(hasher_->category(), TransformCategory::kHash);
}

TEST_P(HasherFixture, DigestSizeIsPositive) {
  EXPECT_GT(hasher_->digest_size_bytes(), 0u);
}

TEST_P(HasherFixture, OneShot_OutputSizeMatchesDigestSize) {
  const std::string kData = "The quick brown fox jumps over the lazy dog";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  ByteBuffer digest = hasher_->Hash(src);
  EXPECT_EQ(digest.size(), hasher_->digest_size_bytes());
}

TEST_P(HasherFixture, HexOutput_LengthAndCharset) {
  const std::string kData = "hex test";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  std::string hex = hasher_->HashHex(src);
  EXPECT_EQ(hex.size(), hasher_->digest_size_bytes() * 2);
  for (char c : hex) {
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
        << "Non-hex char: " << c;
  }
}

TEST_P(HasherFixture, Determinism) {
  const std::string kData = "deterministic hash test";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  EXPECT_EQ(hasher_->Hash(src), hasher_->Hash(src));
}

TEST_P(HasherFixture, AvalancheEffect_SingleBitChange) {
  // Flip one bit in the input — output should be completely different.
  std::string kA(32, 'A');
  std::string kB = kA;
  kB[0] ^= 0x01;

  ByteBuffer ha = hasher_->Hash(
      ByteSpan(reinterpret_cast<const uint8_t *>(kA.data()), kA.size()));
  ByteBuffer hb = hasher_->Hash(
      ByteSpan(reinterpret_cast<const uint8_t *>(kB.data()), kB.size()));

  EXPECT_NE(ha, hb);
}

TEST_P(HasherFixture, Streaming_MatchesOneShot) {
  const std::string kData =
      "streaming consistency test for cryptographic hashing algorithms";
  ByteSpan full(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  ByteBuffer one_shot = hasher_->Hash(full);

  // Stream in 4 chunks.
  hasher_->Init();
  const size_t step = kData.size() / 4;
  for (size_t i = 0; i < 4; ++i) {
    size_t off = i * step;
    size_t len = (i == 3) ? (kData.size() - off) : step;
    hasher_->Update(
        ByteSpan(reinterpret_cast<const uint8_t *>(kData.data() + off), len));
  }
  ByteBuffer streaming = hasher_->Finalize();

  EXPECT_EQ(one_shot, streaming);
}

TEST_P(HasherFixture, Streaming_FinalizeResetsState) {
  const std::string kData = "reset test";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kData.data()), kData.size());

  hasher_->Init();
  hasher_->Update(src);
  ByteBuffer first = hasher_->Finalize();

  // Second run should produce identical output.
  hasher_->Init();
  hasher_->Update(src);
  ByteBuffer second = hasher_->Finalize();

  EXPECT_EQ(first, second);
}

TEST_P(HasherFixture, EmptyInput_ReturnsFixedSizeDigest) {
  ByteBuffer digest = hasher_->Hash(ByteSpan{});
  EXPECT_EQ(digest.size(), hasher_->digest_size_bytes());
}

// ---------------------------------------------------------------------------
// Keyed-hash tests
// ---------------------------------------------------------------------------

TEST_P(HasherFixture, KeyedHash_ProducesOutputWhenSupported) {
  if (!hasher_->supports_keyed_mode()) {
    GTEST_SKIP() << hasher_->name() << " does not support keyed mode.";
  }

  const std::vector<uint8_t> kKey(32, 0xAB);
  const std::string kData = "keyed hash test message";
  ByteSpan key_span(kKey.data(), kKey.size());
  ByteSpan data_span(reinterpret_cast<const uint8_t *>(kData.data()),
                     kData.size());

  ByteBuffer out;
  ASSERT_TRUE(hasher_->HashKeyed(key_span, data_span, out));
  EXPECT_GT(out.size(), 0u);
}

TEST_P(HasherFixture, KeyedHash_DifferentKeysProduceDifferentOutputs) {
  if (!hasher_->supports_keyed_mode()) {
    GTEST_SKIP() << hasher_->name() << " does not support keyed mode.";
  }

  const std::string kData = "same message, different keys";
  ByteSpan data_span(reinterpret_cast<const uint8_t *>(kData.data()),
                     kData.size());

  const std::vector<uint8_t> kKey1(32, 0x01);
  const std::vector<uint8_t> kKey2(32, 0x02);

  ByteBuffer out1, out2;
  ASSERT_TRUE(hasher_->HashKeyed(ByteSpan(kKey1.data(), kKey1.size()),
                                 data_span, out1));
  ASSERT_TRUE(hasher_->HashKeyed(ByteSpan(kKey2.data(), kKey2.size()),
                                 data_span, out2));

  EXPECT_NE(out1, out2);
}

TEST_P(HasherFixture, KeyedHash_SameKeyAndDataIsConsistent) {
  if (!hasher_->supports_keyed_mode()) {
    GTEST_SKIP() << hasher_->name() << " does not support keyed mode.";
  }

  const std::vector<uint8_t> kKey(32, 0x55);
  const std::string kData = "consistent keyed hash";
  ByteSpan key_span(kKey.data(), kKey.size());
  ByteSpan data_span(reinterpret_cast<const uint8_t *>(kData.data()),
                     kData.size());

  ByteBuffer out1, out2;
  ASSERT_TRUE(hasher_->HashKeyed(key_span, data_span, out1));
  ASSERT_TRUE(hasher_->HashKeyed(key_span, data_span, out2));

  EXPECT_EQ(out1, out2);
}

// ---------------------------------------------------------------------------
// Instantiate for all hash types
// ---------------------------------------------------------------------------
INSTANTIATE_TEST_SUITE_P(AllHashers, HasherFixture,
                         ::testing::Values(HashType::kBLAKE3, HashType::kSHA256,
                                           HashType::kSHA384, HashType::kSHA512,
                                           HashType::kSHA3_256,
                                           HashType::kSHA3_384,
                                           HashType::kSHA3_512),
                         [](const ::testing::TestParamInfo<HashType> &info) {
                           switch (info.param) {
                           case HashType::kBLAKE3:
                             return "BLAKE3";
                           case HashType::kSHA256:
                             return "SHA256";
                           case HashType::kSHA384:
                             return "SHA384";
                           case HashType::kSHA512:
                             return "SHA512";
                           case HashType::kSHA3_256:
                             return "SHA3_256";
                           case HashType::kSHA3_384:
                             return "SHA3_384";
                           case HashType::kSHA3_512:
                             return "SHA3_512";
                           }
                           return "Unknown";
                         });

// ---------------------------------------------------------------------------
// SHA-256 known-value test (NIST FIPS 180-4 test vector)
// ---------------------------------------------------------------------------
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
TEST(Sha256KnownValue, NistTestVector_EmptyString) {
  auto hasher = TransformerFactory::CreateHasher(HashType::kSHA256);
  ASSERT_NE(hasher, nullptr);

  // SHA-256("") =
  // e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  const std::string kExpected = "e3b0c44298fc1c149afbf4c8996fb924"
                                "27ae41e4649b934ca495991b7852b855";

  EXPECT_EQ(hasher->HashHex(ByteSpan{}), kExpected);
}

TEST(Sha256KnownValue, NistTestVector_ABC) {
  auto hasher = TransformerFactory::CreateHasher(HashType::kSHA256);
  ASSERT_NE(hasher, nullptr);

  // SHA-256("abc") = ba7816bf...
  const std::string kInput = "abc";
  ByteSpan src(reinterpret_cast<const uint8_t *>(kInput.data()), kInput.size());
  const std::string kExpected =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

  EXPECT_EQ(hasher->HashHex(src), kExpected);
}
#endif

// ---------------------------------------------------------------------------
// BLAKE3 known-value test
// ---------------------------------------------------------------------------
#if defined(UTILS_TRANSFORMATION_HAS_BLAKE3)
TEST(Blake3KnownValue, EmptyInput) {
  auto hasher = TransformerFactory::CreateHasher(HashType::kBLAKE3);
  ASSERT_NE(hasher, nullptr);

  // Official BLAKE3 test vector: BLAKE3("") first 32 bytes.
  const std::string kExpected =
      "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262";

  EXPECT_EQ(hasher->HashHex(ByteSpan{}), kExpected);
}
#endif
