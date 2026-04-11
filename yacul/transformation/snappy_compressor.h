#ifndef UTILS_TRANSFORMATION_SNAPPY_COMPRESSOR_H
#define UTILS_TRANSFORMATION_SNAPPY_COMPRESSOR_H

// ---------------------------------------------------------------------------
// ICompressor adapter for Google Snappy (github.com/google/snappy).
// ---------------------------------------------------------------------------

#include "yacul/transformation/i_data_transformer.h"

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// SnappyCompressor
//
// Snappy does not support compression levels; set_compression_level() is a
// no-op and compression_level() always returns 0.
//
// Thread safety: Compress() / Decompress() are stateless and thread-safe.
// ---------------------------------------------------------------------------
class SnappyCompressor final : public ICompressor {
public:
  SnappyCompressor() = default;
  ~SnappyCompressor() override = default;

  std::string_view name() const override;
  std::string_view library_version() const override;
  bool is_hardware_accelerated() const override;

  bool Compress(ByteSpan src, ByteBuffer &dst) const override;
  bool Decompress(ByteSpan src, ByteBuffer &dst,
                  size_t original_size) const override;
  size_t MaxCompressedSize(size_t uncompressed_size) const override;

  // Snappy has no levels; always 0.
  int compression_level() const override;
  void set_compression_level(int level) override;
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_SNAPPY_COMPRESSOR_H
