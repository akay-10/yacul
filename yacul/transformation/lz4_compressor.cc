#include "yacul/transformation/lz4_compressor.h"

#include <cstring>
#include <lz4.h>
#include <lz4frame.h>
#include <lz4hc.h>
#include <string>

using namespace std;

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Lz4Compressor::Lz4Compressor(int level) : level_(level) {}

Lz4Compressor::~Lz4Compressor() = default;

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

string_view Lz4Compressor::name() const { return "LZ4"; }

string_view Lz4Compressor::library_version() const {
  static const string kVersion = [] {
    int v = LZ4_versionNumber();
    return to_string(v / 10000) + "." + to_string((v / 100) % 100) + "." +
           to_string(v % 100);
  }();
  return kVersion;
}

bool Lz4Compressor::is_hardware_accelerated() const {
  // LZ4 uses hand-written SIMD; no explicit CPU feature required.
  return false;
}

// ---------------------------------------------------------------------------
// Compression
// ---------------------------------------------------------------------------

bool Lz4Compressor::Compress(ByteSpan src, ByteBuffer &dst) const {
  LZ4F_preferences_t prefs{};
  prefs.frameInfo.contentSize = src.size();
  prefs.compressionLevel = level_;

  const size_t bound = LZ4F_compressFrameBound(src.size(), &prefs);
  dst.resize(bound);

  const size_t written =
    LZ4F_compressFrame(dst.data(), dst.size(), src.data(), src.size(), &prefs);

  if (LZ4F_isError(written))
    return false;
  dst.resize(written);
  return true;
}

// ---------------------------------------------------------------------------
// Decompression
// ---------------------------------------------------------------------------

bool Lz4Compressor::Decompress(ByteSpan src, ByteBuffer &dst,
                               size_t original_size) const {
  LZ4F_dctx *ctx = nullptr;
  if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION)))
    return false;

  // Reserve a generous buffer; grow if needed.
  size_t out_capacity = original_size > 0 ? original_size : src.size() * 4;
  dst.resize(out_capacity);

  size_t src_consumed = src.size();
  size_t dst_produced = dst.size();

  LZ4F_decompressOptions_t opts{};
  size_t ret = LZ4F_decompress(ctx, dst.data(), &dst_produced, src.data(),
                               &src_consumed, &opts);

  LZ4F_freeDecompressionContext(ctx);

  if (LZ4F_isError(ret))
    return false;
  dst.resize(dst_produced);
  return true;
}

// ---------------------------------------------------------------------------
// Bound
// ---------------------------------------------------------------------------

size_t Lz4Compressor::MaxCompressedSize(size_t uncompressed_size) const {
  LZ4F_preferences_t prefs{};
  prefs.compressionLevel = level_;
  return LZ4F_compressFrameBound(uncompressed_size, &prefs);
}

// ---------------------------------------------------------------------------
// Level
// ---------------------------------------------------------------------------

int Lz4Compressor::compression_level() const { return level_; }

void Lz4Compressor::set_compression_level(int level) { level_ = level; }

} // namespace transformation
} // namespace utils
