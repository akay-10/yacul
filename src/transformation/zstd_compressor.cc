#include "zstd_compressor.h"

#include <zstd.h>

#include <cassert>
#include <string>

using namespace std;

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ZstdCompressor::ZstdCompressor(int level)
    : level_(level), cctx_(ZSTD_createCCtx()), dctx_(ZSTD_createDCtx()) {
  assert(cctx_ && "ZSTD_createCCtx failed");
  assert(dctx_ && "ZSTD_createDCtx failed");
}

ZstdCompressor::~ZstdCompressor() {
  ZSTD_freeCCtx(static_cast<ZSTD_CCtx *>(cctx_));
  ZSTD_freeDCtx(static_cast<ZSTD_DCtx *>(dctx_));
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

string_view ZstdCompressor::name() const { return "Zstd"; }

string_view ZstdCompressor::library_version() const {
  static const string kVersion = [] {
    unsigned v = ZSTD_versionNumber();
    return to_string(v / 10000) + "." + to_string((v / 100) % 100) + "." +
           to_string(v % 100);
  }();
  return kVersion;
}

bool ZstdCompressor::is_hardware_accelerated() const {
  // Zstd uses BMI2 / SSE4 paths detected at runtime.
  return false;
}

// ---------------------------------------------------------------------------
// Compression
// ---------------------------------------------------------------------------

bool ZstdCompressor::Compress(ByteSpan src, ByteBuffer &dst) const {
  auto *ctx = static_cast<ZSTD_CCtx *>(cctx_);
  dst.resize(ZSTD_compressBound(src.size()));

  const size_t written = ZSTD_compressCCtx(ctx, dst.data(), dst.size(),
                                           src.data(), src.size(), level_);

  if (ZSTD_isError(written))
    return false;
  dst.resize(written);
  return true;
}

// ---------------------------------------------------------------------------
// Decompression
// ---------------------------------------------------------------------------

bool ZstdCompressor::Decompress(ByteSpan src, ByteBuffer &dst,
                                size_t original_size) const {
  auto *ctx = static_cast<ZSTD_DCtx *>(dctx_);

  // Prefer the embedded content size when available.
  unsigned long long hint = ZSTD_getFrameContentSize(src.data(), src.size());

  size_t capacity =
      (hint != ZSTD_CONTENTSIZE_UNKNOWN && hint != ZSTD_CONTENTSIZE_ERROR)
          ? static_cast<size_t>(hint)
          : (original_size > 0 ? original_size : src.size() * 4);
  dst.resize(capacity);

  const size_t written =
      ZSTD_decompressDCtx(ctx, dst.data(), dst.size(), src.data(), src.size());

  if (ZSTD_isError(written))
    return false;
  dst.resize(written);
  return true;
}

// ---------------------------------------------------------------------------
// Bound
// ---------------------------------------------------------------------------

size_t ZstdCompressor::MaxCompressedSize(size_t uncompressed_size) const {
  return ZSTD_compressBound(uncompressed_size);
}

// ---------------------------------------------------------------------------
// Level
// ---------------------------------------------------------------------------

int ZstdCompressor::compression_level() const { return level_; }

void ZstdCompressor::set_compression_level(int level) { level_ = level; }

} // namespace transformation
} // namespace utils
