#ifndef UTILS_TRANSFORMATION_LZ4_COMPRESSOR_H
#define UTILS_TRANSFORMATION_LZ4_COMPRESSOR_H

// ---------------------------------------------------------------------------
// ICompressor adapter for LZ4 (github.com/lz4/lz4).
// ---------------------------------------------------------------------------

#include "yacul/transformation/i_data_transformer.h"

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Lz4Compressor
//
// Wraps the LZ4 frame API (LZ4F_*) for safe, self-describing compressed
// streams that include content size and checksums.
//
// Thread safety: Compress() / Decompress() are stateless and safe to call
// from multiple threads concurrently on the same instance.
// set_compression_level() is NOT thread-safe.
// ---------------------------------------------------------------------------
class Lz4Compressor final : public ICompressor {
public:
  // level: 0  → LZ4 default fast (LZ4_ACCELERATION_DEFAULT)
  //        >0 → LZ4HC level (1–12); triggers high-compression path
  explicit Lz4Compressor(int level = 0);
  ~Lz4Compressor() override;

  std::string_view name() const override;
  std::string_view library_version() const override;
  bool is_hardware_accelerated() const override;

  bool Compress(ByteSpan src, ByteBuffer &dst) const override;
  bool Decompress(ByteSpan src, ByteBuffer &dst,
                  size_t original_size) const override;
  size_t MaxCompressedSize(size_t uncompressed_size) const override;

  int compression_level() const override;
  void set_compression_level(int level) override;

private:
  int level_;
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_LZ4_COMPRESSOR_H
