#ifndef UTILS_TRANSFORMATION_TRANSFORMER_FACTORY_H
#define UTILS_TRANSFORMATION_TRANSFORMER_FACTORY_H

// ---------------------------------------------------------------------------
// Factory for creating IDataTransformer instances by name or enum.
// Respects compile-time feature flags (DATATRANSFORM_HAS_*).
// ---------------------------------------------------------------------------

#include <memory>
#include <string_view>
#include <vector>

#include "i_data_transformer.h"

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Compressor algorithms available at compile time
// ---------------------------------------------------------------------------
enum class CompressorType : uint8_t {
  kLZ4 = 0,
  kZstd = 1,
  kSnappy = 2,
};

// ---------------------------------------------------------------------------
// Checksum algorithms available at compile time
// ---------------------------------------------------------------------------
enum class ChecksumType : uint8_t {
  kXXH32 = 0,
  kXXH64 = 1,
  kXXH3_64 = 2,
  kXXH3_128 = 3,
  kCRC32C = 4,
};

// ---------------------------------------------------------------------------
// Hash algorithms available at compile time
// ---------------------------------------------------------------------------
enum class HashType : uint8_t {
  kBLAKE3 = 0,
  kSHA256 = 1,
  kSHA384 = 2,
  kSHA512 = 3,
  kSHA3_256 = 4,
  kSHA3_384 = 5,
  kSHA3_512 = 6,
};

// ---------------------------------------------------------------------------
// TransformerFactory
// ---------------------------------------------------------------------------
class TransformerFactory {
public:
  TransformerFactory() = delete; // Static factory only.

  // -------------------------------------------------------------------------
  // Create a compressor.  Returns nullptr if the type was not compiled in.
  // -------------------------------------------------------------------------
  static std::unique_ptr<ICompressor> CreateCompressor(CompressorType type,
                                                       int level = 0);

  // -------------------------------------------------------------------------
  // Create a checksummer.  Returns nullptr if not compiled in.
  // -------------------------------------------------------------------------
  static std::unique_ptr<IChecksummer> CreateChecksummer(ChecksumType type,
                                                         uint64_t seed = 0);

  // -------------------------------------------------------------------------
  // Create a hasher.  Returns nullptr if not compiled in.
  // -------------------------------------------------------------------------
  static std::unique_ptr<IHasher>
  CreateHasher(HashType type, size_t output_bytes = 0 /*use default*/);

  // -------------------------------------------------------------------------
  // Enumerate all compressors / checksummers / hashers compiled in.
  // -------------------------------------------------------------------------
  static std::vector<CompressorType> AvailableCompressors();
  static std::vector<ChecksumType> AvailableChecksummers();
  static std::vector<HashType> AvailableHashers();

  // -------------------------------------------------------------------------
  // String-based lookup (case-insensitive, e.g. "lz4", "blake3", "sha-256").
  // Returns nullptr on unknown name or missing compile-time support.
  // -------------------------------------------------------------------------
  static std::unique_ptr<ICompressor> CompressorByName(std::string_view name,
                                                       int level = 0);
  static std::unique_ptr<IChecksummer> ChecksummerByName(std::string_view name,
                                                         uint64_t seed = 0);
  static std::unique_ptr<IHasher> HasherByName(std::string_view name,
                                               size_t output_bytes = 0);
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_TRANSFORMER_FACTORY_H
