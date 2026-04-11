#include "yacul/transformation/transformer_factory.h"

#include <algorithm>
#include <cctype>
#include <string>

#if defined(UTILS_TRANSFORMATION_HAS_LZ4)
#include "yacul/transformation/lz4_compressor.h"
#endif
#if defined(UTILS_TRANSFORMATION_HAS_ZSTD)
#include "yacul/transformation/zstd_compressor.h"
#endif
#if defined(UTILS_TRANSFORMATION_HAS_SNAPPY)
#include "yacul/transformation/snappy_compressor.h"
#endif
#if defined(UTILS_TRANSFORMATION_HAS_XXHASH)
#include "yacul/transformation/xxhash_checksummer.h"
#endif
#if defined(UTILS_TRANSFORMATION_HAS_CRC32C)
#include "yacul/transformation/crc32c_checksummer.h"
#endif
#if defined(UTILS_TRANSFORMATION_HAS_BLAKE3)
#include "yacul/transformation/blake3_hasher.h"
#endif
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
#include "yacul/transformation/openssl_sha_hasher.h"
#endif

using namespace std;

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// CreateCompressor
// ---------------------------------------------------------------------------

unique_ptr<ICompressor>
TransformerFactory::CreateCompressor(CompressorType type, int level) {
  switch (type) {
  case CompressorType::kLZ4:
#if defined(UTILS_TRANSFORMATION_HAS_LZ4)
    return make_unique<Lz4Compressor>(level);
#else
    return nullptr;
#endif

  case CompressorType::kZstd:
#if defined(UTILS_TRANSFORMATION_HAS_ZSTD)
    return make_unique<ZstdCompressor>(level > 0 ? level : 3);
#else
    return nullptr;
#endif

  case CompressorType::kSnappy:
#if defined(UTILS_TRANSFORMATION_HAS_SNAPPY)
    return make_unique<SnappyCompressor>();
#else
    return nullptr;
#endif
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// CreateChecksummer
// ---------------------------------------------------------------------------

unique_ptr<IChecksummer>
TransformerFactory::CreateChecksummer(ChecksumType type, uint64_t seed) {
  switch (type) {
#if defined(UTILS_TRANSFORMATION_HAS_XXHASH)
  case ChecksumType::kXXH32:
    return make_unique<XxHashChecksummer>(XxHashVariant::kXXH32, seed);
  case ChecksumType::kXXH64:
    return make_unique<XxHashChecksummer>(XxHashVariant::kXXH64, seed);
  case ChecksumType::kXXH3_64:
    return make_unique<XxHashChecksummer>(XxHashVariant::kXXH3_64, seed);
  case ChecksumType::kXXH3_128:
    return make_unique<XxHashChecksummer>(XxHashVariant::kXXH3_128, seed);
#else
  case ChecksumType::kXXH32:
  case ChecksumType::kXXH64:
  case ChecksumType::kXXH3_64:
  case ChecksumType::kXXH3_128:
    return nullptr;
#endif

  case ChecksumType::kCRC32C:
#if defined(UTILS_TRANSFORMATION_HAS_CRC32C)
    return make_unique<Crc32cChecksummer>();
#else
    return nullptr;
#endif
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// CreateHasher
// ---------------------------------------------------------------------------

unique_ptr<IHasher> TransformerFactory::CreateHasher(HashType type,
                                                     size_t output_bytes) {
  switch (type) {
  case HashType::kBLAKE3:
#if defined(UTILS_TRANSFORMATION_HAS_BLAKE3)
    return make_unique<Blake3Hasher>(output_bytes > 0 ? output_bytes : 32);
#else
    return nullptr;
#endif

  case HashType::kSHA256:
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
    return make_unique<OpensslShaHasher>(ShaAlgorithm::kSHA256);
#else
    return nullptr;
#endif

  case HashType::kSHA384:
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
    return make_unique<OpensslShaHasher>(ShaAlgorithm::kSHA384);
#else
    return nullptr;
#endif

  case HashType::kSHA512:
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
    return make_unique<OpensslShaHasher>(ShaAlgorithm::kSHA512);
#else
    return nullptr;
#endif

  case HashType::kSHA3_256:
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
    return make_unique<OpensslShaHasher>(ShaAlgorithm::kSHA3_256);
#else
    return nullptr;
#endif

  case HashType::kSHA3_384:
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
    return make_unique<OpensslShaHasher>(ShaAlgorithm::kSHA3_384);
#else
    return nullptr;
#endif

  case HashType::kSHA3_512:
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
    return make_unique<OpensslShaHasher>(ShaAlgorithm::kSHA3_512);
#else
    return nullptr;
#endif
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Enumerate available types
// ---------------------------------------------------------------------------

vector<CompressorType> TransformerFactory::AvailableCompressors() {
  vector<CompressorType> out;
#if defined(UTILS_TRANSFORMATION_HAS_LZ4)
  out.push_back(CompressorType::kLZ4);
#endif
#if defined(UTILS_TRANSFORMATION_HAS_ZSTD)
  out.push_back(CompressorType::kZstd);
#endif
#if defined(UTILS_TRANSFORMATION_HAS_SNAPPY)
  out.push_back(CompressorType::kSnappy);
#endif
  return out;
}

vector<ChecksumType> TransformerFactory::AvailableChecksummers() {
  vector<ChecksumType> out;
#if defined(UTILS_TRANSFORMATION_HAS_XXHASH)
  out.push_back(ChecksumType::kXXH32);
  out.push_back(ChecksumType::kXXH64);
  out.push_back(ChecksumType::kXXH3_64);
  out.push_back(ChecksumType::kXXH3_128);
#endif
#if defined(UTILS_TRANSFORMATION_HAS_CRC32C)
  out.push_back(ChecksumType::kCRC32C);
#endif
  return out;
}

vector<HashType> TransformerFactory::AvailableHashers() {
  vector<HashType> out;
#if defined(UTILS_TRANSFORMATION_HAS_BLAKE3)
  out.push_back(HashType::kBLAKE3);
#endif
#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
  out.push_back(HashType::kSHA256);
  out.push_back(HashType::kSHA384);
  out.push_back(HashType::kSHA512);
  out.push_back(HashType::kSHA3_256);
  out.push_back(HashType::kSHA3_384);
  out.push_back(HashType::kSHA3_512);
#endif
  return out;
}

// ---------------------------------------------------------------------------
// String-based lookup helpers
// ---------------------------------------------------------------------------

namespace {
string ToLower(string_view s) {
  string out(s);
  transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return tolower(c); });
  return out;
}
} // namespace

unique_ptr<ICompressor> TransformerFactory::CompressorByName(string_view name,
                                                             int level) {
  const string n = ToLower(name);
  if (n == "lz4")
    return CreateCompressor(CompressorType::kLZ4, level);
  if (n == "zstd")
    return CreateCompressor(CompressorType::kZstd, level);
  if (n == "snappy")
    return CreateCompressor(CompressorType::kSnappy, level);
  return nullptr;
}

unique_ptr<IChecksummer> TransformerFactory::ChecksummerByName(string_view name,
                                                               uint64_t seed) {
  const string n = ToLower(name);
  if (n == "xxh32" || n == "xxhash32")
    return CreateChecksummer(ChecksumType::kXXH32, seed);
  if (n == "xxh64" || n == "xxhash64")
    return CreateChecksummer(ChecksumType::kXXH64, seed);
  if (n == "xxh3-64" || n == "xxhash3-64")
    return CreateChecksummer(ChecksumType::kXXH3_64, seed);
  if (n == "xxh3-128" || n == "xxhash3-128" || n == "xxh3_128")
    return CreateChecksummer(ChecksumType::kXXH3_128, seed);
  if (n == "crc32c" || n == "crc32-c")
    return CreateChecksummer(ChecksumType::kCRC32C, seed);
  return nullptr;
}

unique_ptr<IHasher> TransformerFactory::HasherByName(string_view name,
                                                     size_t output_bytes) {
  const string n = ToLower(name);
  if (n == "blake3")
    return CreateHasher(HashType::kBLAKE3, output_bytes);
  if (n == "sha-256" || n == "sha256")
    return CreateHasher(HashType::kSHA256, output_bytes);
  if (n == "sha-384" || n == "sha384")
    return CreateHasher(HashType::kSHA384, output_bytes);
  if (n == "sha-512" || n == "sha512")
    return CreateHasher(HashType::kSHA512, output_bytes);
  if (n == "sha3-256" || n == "sha-3-256")
    return CreateHasher(HashType::kSHA3_256, output_bytes);
  if (n == "sha3-384" || n == "sha-3-384")
    return CreateHasher(HashType::kSHA3_384, output_bytes);
  if (n == "sha3-512" || n == "sha-3-512")
    return CreateHasher(HashType::kSHA3_512, output_bytes);
  return nullptr;
}

} // namespace transformation
} // namespace utils
