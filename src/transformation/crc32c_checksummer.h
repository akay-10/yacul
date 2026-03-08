#ifndef UTILS_TRANSFORMATION_CRC32C_CHECKSUMMER_H
#define UTILS_TRANSFORMATION_CRC32C_CHECKSUMMER_H

// ---------------------------------------------------------------------------
// IChecksummer adapter for CRC32C (github.com/google/crc32c).
// Uses SSE4.2 / ARMv8 CRC hardware instructions when available.
// ---------------------------------------------------------------------------

#include "i_data_transformer.h"

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Crc32cChecksummer
//
// CRC32C (Castagnoli polynomial) — the variant used in iSCSI, SCTP,
// NVMe, and ext4.  32-bit output, hardware-accelerated on modern CPUs.
//
// Thread safety: Compute() / ComputeHex() are thread-safe.
// Streaming (Reset/Update/Finalize) is NOT thread-safe per instance.
// ---------------------------------------------------------------------------
class Crc32cChecksummer final : public IChecksummer {
public:
  Crc32cChecksummer() = default;
  ~Crc32cChecksummer() override = default;

  std::string_view name() const override;
  std::string_view library_version() const override;
  bool is_hardware_accelerated() const override;

  size_t digest_size_bytes() const override; // 4

  ByteBuffer Compute(ByteSpan data) const override;
  std::string ComputeHex(ByteSpan data) const override;

  void Reset() override;
  void Update(ByteSpan data) override;
  ByteBuffer Finalize() override;
  std::string FinalizeHex() override;

private:
  uint32_t state_ = 0;
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_CRC32C_CHECKSUMMER_H
