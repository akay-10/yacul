#include "yacul/transformation/snappy_compressor.h"

#include <snappy.h>
#include <string>

using namespace std;

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

string_view SnappyCompressor::name() const { return "Snappy"; }

string_view SnappyCompressor::library_version() const {
  // Snappy exposes no runtime version query; use compile-time macros.
  static const string kVersion = to_string(SNAPPY_MAJOR) + "." +
                                 to_string(SNAPPY_MINOR) + "." +
                                 to_string(SNAPPY_PATCHLEVEL);
  return kVersion;
}

bool SnappyCompressor::is_hardware_accelerated() const {
  return false; // Snappy uses hand-vectorized code, no CPU flag required.
}

// ---------------------------------------------------------------------------
// Compression
// ---------------------------------------------------------------------------

bool SnappyCompressor::Compress(ByteSpan src, ByteBuffer &dst) const {
  size_t bound = snappy::MaxCompressedLength(src.size());
  dst.resize(bound);

  size_t written = 0;
  snappy::RawCompress(reinterpret_cast<const char *>(src.data()), src.size(),
                      reinterpret_cast<char *>(dst.data()), &written);

  dst.resize(written);
  return written > 0;
}

// ---------------------------------------------------------------------------
// Decompression
// ---------------------------------------------------------------------------

bool SnappyCompressor::Decompress(ByteSpan src, ByteBuffer &dst,
                                  size_t /*original_size*/) const {
  size_t uncompressed_len = 0;
  if (!snappy::GetUncompressedLength(reinterpret_cast<const char *>(src.data()),
                                     src.size(), &uncompressed_len)) {
    return false;
  }
  dst.resize(uncompressed_len);
  return snappy::RawUncompress(reinterpret_cast<const char *>(src.data()),
                               src.size(),
                               reinterpret_cast<char *>(dst.data()));
}

// ---------------------------------------------------------------------------
// Bound
// ---------------------------------------------------------------------------

size_t SnappyCompressor::MaxCompressedSize(size_t uncompressed_size) const {
  return snappy::MaxCompressedLength(uncompressed_size);
}

// ---------------------------------------------------------------------------
// Level (no-op for Snappy)
// ---------------------------------------------------------------------------

int SnappyCompressor::compression_level() const { return 0; }

void SnappyCompressor::set_compression_level(int /*level*/) {
  // Intentional no-op — Snappy has a single fixed speed/ratio trade-off.
}

} // namespace transformation
} // namespace utils
