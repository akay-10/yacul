#ifndef UTILS_TRANSFORMATION_BLAKE3_HASHER_H
#define UTILS_TRANSFORMATION_BLAKE3_HASHER_H

// ---------------------------------------------------------------------------
// IHasher adapter for BLAKE3 (github.com/BLAKE3-team/BLAKE3).
// Fastest cryptographic hash; supports keyed (MAC) and key-derivation modes.
// ---------------------------------------------------------------------------

#include "i_data_transformer.h"

#include <memory>

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Blake3Hasher
//
// Default output is 32 bytes; the caller may request any length via
// set_output_length().
//
// Thread safety: one-shot Hash() / HashHex() are thread-safe.
// Streaming (Init/Update/Finalize) is NOT thread-safe per instance.
// ---------------------------------------------------------------------------
class Blake3Hasher final : public IHasher {
public:
  // output_length: desired digest size in bytes (default 32).
  explicit Blake3Hasher(size_t output_length = 32);
  ~Blake3Hasher() override;

  Blake3Hasher(const Blake3Hasher &) = delete;
  Blake3Hasher &operator=(const Blake3Hasher &) = delete;

  std::string_view name() const override;
  std::string_view library_version() const override;
  bool is_hardware_accelerated() const override;

  size_t digest_size_bytes() const override;
  bool supports_keyed_mode() const override;

  // Key must be exactly 32 bytes.
  bool HashKeyed(ByteSpan key, ByteSpan data, ByteBuffer &out) const override;

  ByteBuffer Hash(ByteSpan data) const override;
  std::string HashHex(ByteSpan data) const override;

  void Init() override;
  bool InitKeyed(ByteSpan key) override;
  void Update(ByteSpan data) override;
  ByteBuffer Finalize() override;
  std::string FinalizeHex() override;

  // Change desired output length for subsequent operations.
  void set_output_length(size_t len);

private:
  size_t output_length_;
  // Opaque hasher state (blake3_hasher struct).
  void *state_;
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_BLAKE3_HASHER_H
