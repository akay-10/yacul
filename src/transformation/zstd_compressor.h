#ifndef UTILS_TRANSFORMATION_ZSTD_COMPRESSOR_H
#define UTILS_TRANSFORMATION_ZSTD_COMPRESSOR_H

// ---------------------------------------------------------------------------
// ICompressor adapter for Zstandard (github.com/facebook/zstd).
// ---------------------------------------------------------------------------

#include "i_data_transformer.h"

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// ZstdCompressor
//
// Wraps the ZSTD streaming API with a reusable CCtx / DCtx pair allocated
// at construction time (avoids per-call malloc).
//
// Thread safety: NOT thread-safe. Create one instance per thread.
// ---------------------------------------------------------------------------
class ZstdCompressor final : public ICompressor {
public:
  // level: ZSTD_CLEVEL_DEFAULT (3) is the recommended starting point.
  // Range: 1 (fastest) – 22 (smallest).
  explicit ZstdCompressor(int level = 3);
  ~ZstdCompressor() override;

  // Non-copyable (owns raw ctx pointers).
  ZstdCompressor(const ZstdCompressor &) = delete;
  ZstdCompressor &operator=(const ZstdCompressor &) = delete;

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
  // Opaque pointers — avoids including zstd.h in the public header.
  void *cctx_;
  void *dctx_;
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_ZSTD_COMPRESSOR_H
