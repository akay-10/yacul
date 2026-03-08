#include "xxhash_checksummer.h"
#include "hex_utils.h"

#include <xxhash.h>

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace std;

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

size_t DigestSizeFor(XxHashVariant v) {
  switch (v) {
  case XxHashVariant::kXXH32:
    return 4;
  case XxHashVariant::kXXH64:
    return 8;
  case XxHashVariant::kXXH3_64:
    return 8;
  case XxHashVariant::kXXH3_128:
    return 16;
  }
  return 8;
}

string_view VariantName(XxHashVariant v) {
  switch (v) {
  case XxHashVariant::kXXH32:
    return "xxHash-XXH32";
  case XxHashVariant::kXXH64:
    return "xxHash-XXH64";
  case XxHashVariant::kXXH3_64:
    return "xxHash-XXH3-64";
  case XxHashVariant::kXXH3_128:
    return "xxHash-XXH3-128";
  }
  return "xxHash-unknown";
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

XxHashChecksummer::XxHashChecksummer(XxHashVariant variant, uint64_t seed)
    : variant_(variant), seed_(seed), state_(nullptr) {
  AllocState();
}

XxHashChecksummer::~XxHashChecksummer() { FreeState(); }

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------

void XxHashChecksummer::AllocState() {
  FreeState();
  switch (variant_) {
  case XxHashVariant::kXXH32:
    state_ = XXH32_createState();
    XXH32_reset(static_cast<XXH32_state_t *>(state_),
                static_cast<uint32_t>(seed_));
    break;
  case XxHashVariant::kXXH64:
    state_ = XXH64_createState();
    XXH64_reset(static_cast<XXH64_state_t *>(state_), seed_);
    break;
  case XxHashVariant::kXXH3_64:
    state_ = XXH3_createState();
    XXH3_64bits_reset_withSeed(static_cast<XXH3_state_t *>(state_), seed_);
    break;
  case XxHashVariant::kXXH3_128:
    state_ = XXH3_createState();
    XXH3_128bits_reset_withSeed(static_cast<XXH3_state_t *>(state_), seed_);
    break;
  }
  assert(state_ && "xxHash state allocation failed");
}

void XxHashChecksummer::FreeState() {
  if (!state_)
    return;
  switch (variant_) {
  case XxHashVariant::kXXH32:
    XXH32_freeState(static_cast<XXH32_state_t *>(state_));
    break;
  case XxHashVariant::kXXH64:
    XXH64_freeState(static_cast<XXH64_state_t *>(state_));
    break;
  case XxHashVariant::kXXH3_64:
  case XxHashVariant::kXXH3_128:
    XXH3_freeState(static_cast<XXH3_state_t *>(state_));
    break;
  }
  state_ = nullptr;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

string_view XxHashChecksummer::name() const { return VariantName(variant_); }

string_view XxHashChecksummer::library_version() const {
  static const string kVersion = [] {
    unsigned v = XXH_versionNumber();
    return to_string(v / 10000) + "." + to_string((v / 100) % 100) + "." +
           to_string(v % 100);
  }();
  return kVersion;
}

bool XxHashChecksummer::is_hardware_accelerated() const {
  // XXH3 uses AVX2 / NEON / SSE2 at runtime; XXH32/64 are scalar.
  return variant_ == XxHashVariant::kXXH3_64 ||
         variant_ == XxHashVariant::kXXH3_128;
}

size_t XxHashChecksummer::digest_size_bytes() const {
  return DigestSizeFor(variant_);
}

// ---------------------------------------------------------------------------
// One-shot
// ---------------------------------------------------------------------------

ByteBuffer XxHashChecksummer::Compute(ByteSpan data) const {
  ByteBuffer out(DigestSizeFor(variant_));
  switch (variant_) {
  case XxHashVariant::kXXH32: {
    uint32_t h = XXH32(data.data(), data.size(), static_cast<uint32_t>(seed_));
    memcpy(out.data(), &h, 4);
    break;
  }
  case XxHashVariant::kXXH64: {
    uint64_t h = XXH64(data.data(), data.size(), seed_);
    memcpy(out.data(), &h, 8);
    break;
  }
  case XxHashVariant::kXXH3_64: {
    uint64_t h = XXH3_64bits_withSeed(data.data(), data.size(), seed_);
    memcpy(out.data(), &h, 8);
    break;
  }
  case XxHashVariant::kXXH3_128: {
    XXH128_hash_t h = XXH3_128bits_withSeed(data.data(), data.size(), seed_);
    memcpy(out.data(), &h, 16);
    break;
  }
  }
  return out;
}

string XxHashChecksummer::ComputeHex(ByteSpan data) const {
  return ToHex(Compute(data));
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

void XxHashChecksummer::Reset() {
  switch (variant_) {
  case XxHashVariant::kXXH32:
    XXH32_reset(static_cast<XXH32_state_t *>(state_),
                static_cast<uint32_t>(seed_));
    break;
  case XxHashVariant::kXXH64:
    XXH64_reset(static_cast<XXH64_state_t *>(state_), seed_);
    break;
  case XxHashVariant::kXXH3_64:
    XXH3_64bits_reset_withSeed(static_cast<XXH3_state_t *>(state_), seed_);
    break;
  case XxHashVariant::kXXH3_128:
    XXH3_128bits_reset_withSeed(static_cast<XXH3_state_t *>(state_), seed_);
    break;
  }
}

void XxHashChecksummer::Update(ByteSpan data) {
  switch (variant_) {
  case XxHashVariant::kXXH32:
    XXH32_update(static_cast<XXH32_state_t *>(state_), data.data(),
                 data.size());
    break;
  case XxHashVariant::kXXH64:
    XXH64_update(static_cast<XXH64_state_t *>(state_), data.data(),
                 data.size());
    break;
  case XxHashVariant::kXXH3_64:
    XXH3_64bits_update(static_cast<XXH3_state_t *>(state_), data.data(),
                       data.size());
    break;
  case XxHashVariant::kXXH3_128:
    XXH3_128bits_update(static_cast<XXH3_state_t *>(state_), data.data(),
                        data.size());
    break;
  }
}

ByteBuffer XxHashChecksummer::Finalize() {
  ByteBuffer out(DigestSizeFor(variant_));
  switch (variant_) {
  case XxHashVariant::kXXH32: {
    uint32_t h = XXH32_digest(static_cast<XXH32_state_t *>(state_));
    memcpy(out.data(), &h, 4);
    break;
  }
  case XxHashVariant::kXXH64: {
    uint64_t h = XXH64_digest(static_cast<XXH64_state_t *>(state_));
    memcpy(out.data(), &h, 8);
    break;
  }
  case XxHashVariant::kXXH3_64: {
    uint64_t h = XXH3_64bits_digest(static_cast<XXH3_state_t *>(state_));
    memcpy(out.data(), &h, 8);
    break;
  }
  case XxHashVariant::kXXH3_128: {
    XXH128_hash_t h = XXH3_128bits_digest(static_cast<XXH3_state_t *>(state_));
    memcpy(out.data(), &h, 16);
    break;
  }
  }
  Reset();
  return out;
}

string XxHashChecksummer::FinalizeHex() { return ToHex(Finalize()); }

} // namespace transformation
} // namespace utils
