#include "blake3_hasher.h"
#include "hex_utils.h"

#include <blake3.h>

#include <cassert>
#include <cstring>
#include <new>
#include <string>

using namespace std;

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Blake3Hasher::Blake3Hasher(size_t output_length)
    : output_length_(output_length),
      state_(::operator new(sizeof(blake3_hasher))) {
  assert(state_);
  blake3_hasher_init(static_cast<blake3_hasher *>(state_));
}

Blake3Hasher::~Blake3Hasher() { ::operator delete(state_); }

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

string_view Blake3Hasher::name() const { return "BLAKE3"; }

string_view Blake3Hasher::library_version() const {
  // BLAKE3_VERSION_STRING is a compile-time macro.
  return BLAKE3_VERSION_STRING;
}

bool Blake3Hasher::is_hardware_accelerated() const {
  // BLAKE3 ships AVX-512, AVX2, SSE4.1, SSE2, NEON dispatch.
  return true;
}

size_t Blake3Hasher::digest_size_bytes() const { return output_length_; }

bool Blake3Hasher::supports_keyed_mode() const { return true; }

// ---------------------------------------------------------------------------
// One-shot
// ---------------------------------------------------------------------------

ByteBuffer Blake3Hasher::Hash(ByteSpan data) const {
  blake3_hasher local;
  blake3_hasher_init(&local);
  blake3_hasher_update(&local, data.data(), data.size());
  ByteBuffer out(output_length_);
  blake3_hasher_finalize(&local, out.data(), output_length_);
  return out;
}

string Blake3Hasher::HashHex(ByteSpan data) const {
  return ToHex(Hash(data));
}

bool Blake3Hasher::HashKeyed(ByteSpan key, ByteSpan data,
                             ByteBuffer &out) const {
  if (key.size() != BLAKE3_KEY_LEN)
    return false;
  blake3_hasher local;
  blake3_hasher_init_keyed(&local, key.data());
  blake3_hasher_update(&local, data.data(), data.size());
  out.resize(output_length_);
  blake3_hasher_finalize(&local, out.data(), output_length_);
  return true;
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

void Blake3Hasher::Init() {
  blake3_hasher_init(static_cast<blake3_hasher *>(state_));
}

bool Blake3Hasher::InitKeyed(ByteSpan key) {
  if (key.size() != BLAKE3_KEY_LEN)
    return false;
  blake3_hasher_init_keyed(static_cast<blake3_hasher *>(state_), key.data());
  return true;
}

void Blake3Hasher::Update(ByteSpan data) {
  blake3_hasher_update(static_cast<blake3_hasher *>(state_), data.data(),
                       data.size());
}

ByteBuffer Blake3Hasher::Finalize() {
  ByteBuffer out(output_length_);
  blake3_hasher_finalize(static_cast<blake3_hasher *>(state_), out.data(),
                         output_length_);
  Init(); // Reset for reuse.
  return out;
}

string Blake3Hasher::FinalizeHex() { return ToHex(Finalize()); }

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void Blake3Hasher::set_output_length(size_t len) { output_length_ = len; }

} // namespace transformation
} // namespace utils
