#include "crc32c_checksummer.h"
#include "hex_utils.h"

#include <crc32c/crc32c.h>

#include <cstring>
#include <string>

using namespace std;

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

string_view Crc32cChecksummer::name() const { return "CRC32C"; }

string_view Crc32cChecksummer::library_version() const {
  return "google/crc32c";
}

bool Crc32cChecksummer::is_hardware_accelerated() const {
#if defined(__SSE4_2__) || defined(__ARM_FEATURE_CRC32)
  return true;
#else
  return false;
#endif
}

size_t Crc32cChecksummer::digest_size_bytes() const { return 4; }

// ---------------------------------------------------------------------------
// One-shot
// ---------------------------------------------------------------------------

ByteBuffer Crc32cChecksummer::Compute(ByteSpan data) const {
  uint32_t crc = crc32c::Crc32c(data.data(), data.size());
  ByteBuffer out(4);
  memcpy(out.data(), &crc, 4);
  return out;
}

string Crc32cChecksummer::ComputeHex(ByteSpan data) const {
  return ToHex(Compute(data));
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

void Crc32cChecksummer::Reset() { state_ = 0; }

void Crc32cChecksummer::Update(ByteSpan data) {
  state_ = crc32c::Extend(state_, data.data(), data.size());
}

ByteBuffer Crc32cChecksummer::Finalize() {
  ByteBuffer out(4);
  memcpy(out.data(), &state_, 4);
  Reset();
  return out;
}

string Crc32cChecksummer::FinalizeHex() { return ToHex(Finalize()); }

} // namespace transformation
} // namespace utisl
