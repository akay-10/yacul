#ifndef UTILS_TRANSFORMATION_XXHASH_CHECKSUMMER_H
#define UTILS_TRANSFORMATION_XXHASH_CHECKSUMMER_H

// ---------------------------------------------------------------------------
// IChecksummer adapter for xxHash (github.com/Cyan4973/xxHash).
// Supports XXH32, XXH64, XXH3-64, and XXH3-128 variants.
// ---------------------------------------------------------------------------

#include "yacul/transformation/i_data_transformer.h"

#include <memory> // shared_ptr

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// XxHashVariant
// ---------------------------------------------------------------------------
enum class XxHashVariant : uint8_t {
  kXXH32 = 0,    // 32-bit, best on 32-bit platforms
  kXXH64 = 1,    // 64-bit, portable
  kXXH3_64 = 2,  // 64-bit, AVX2/NEON accelerated (recommended)
  kXXH3_128 = 3, // 128-bit, AVX2/NEON accelerated (recommended for keys)
};

// ---------------------------------------------------------------------------
// XxHashChecksummer
//
// Thread safety: one-shot Compute() / ComputeHex() are thread-safe.
// Streaming (Reset/Update/Finalize) is NOT thread-safe per instance.
// ---------------------------------------------------------------------------
class XxHashChecksummer final : public IChecksummer {
public:
  explicit XxHashChecksummer(XxHashVariant variant = XxHashVariant::kXXH3_128,
                             uint64_t seed = 0);
  ~XxHashChecksummer() override;

  XxHashChecksummer(const XxHashChecksummer &) = delete;
  XxHashChecksummer &operator=(const XxHashChecksummer &) = delete;

  std::string_view name() const override;
  std::string_view library_version() const override;
  bool is_hardware_accelerated() const override;

  size_t digest_size_bytes() const override;

  ByteBuffer Compute(ByteSpan data) const override;
  std::string ComputeHex(ByteSpan data) const override;

  void Reset() override;
  void Update(ByteSpan data) override;
  ByteBuffer Finalize() override;
  std::string FinalizeHex() override;

private:
  XxHashVariant variant_;
  uint64_t seed_;
  // Opaque streaming state — allocated in Reset(), freed in destructor.
  void *state_;

  void AllocState();
  void FreeState();
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_XXHASH_CHECKSUMMER_H
