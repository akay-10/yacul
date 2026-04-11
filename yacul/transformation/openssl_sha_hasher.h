#ifndef UTILS_TRANSFORMATION_OPENSSL_SHA_HASHER_H
#define UTILS_TRANSFORMATION_OPENSSL_SHA_HASHER_H

// ---------------------------------------------------------------------------
// IHasher adapter for OpenSSL EVP SHA family (SHA-256, SHA-384, SHA-512,
// SHA-3 variants).  Uses hardware SHA-NI / ARMv8 SHA extensions when
// available.
// ---------------------------------------------------------------------------

#include "yacul/transformation/i_data_transformer.h"

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// ShaAlgorithm
// ---------------------------------------------------------------------------
enum class ShaAlgorithm : uint8_t {
  kSHA256 = 0,
  kSHA384 = 1,
  kSHA512 = 2,
  kSHA3_256 = 3,
  kSHA3_384 = 4,
  kSHA3_512 = 5,
};

// ---------------------------------------------------------------------------
// OpensslShaHasher
//
// FIPS / compliance SHA via OpenSSL's EVP API.  One EVP_MD_CTX is held
// per instance and reused across calls to avoid per-call allocation.
//
// Thread safety: one-shot Hash() / HashHex() are thread-safe.
// Streaming (Init/Update/Finalize) is NOT thread-safe per instance.
// ---------------------------------------------------------------------------
class OpensslShaHasher final : public IHasher {
public:
  explicit OpensslShaHasher(ShaAlgorithm algo = ShaAlgorithm::kSHA256);
  ~OpensslShaHasher() override;

  OpensslShaHasher(const OpensslShaHasher &) = delete;
  OpensslShaHasher &operator=(const OpensslShaHasher &) = delete;

  std::string_view name() const override;
  std::string_view library_version() const override;
  bool is_hardware_accelerated() const override;

  size_t digest_size_bytes() const override;
  bool supports_keyed_mode() const override;

  ByteBuffer Hash(ByteSpan data) const override;
  std::string HashHex(ByteSpan data) const override;

  // HMAC via HMAC-SHA*.  Key can be any length.
  bool HashKeyed(ByteSpan key, ByteSpan data, ByteBuffer &out) const override;

  void Init() override;
  bool InitKeyed(ByteSpan key) override;
  void Update(ByteSpan data) override;
  ByteBuffer Finalize() override;
  std::string FinalizeHex() override;

private:
  ShaAlgorithm algo_;
  void *ctx_; // EVP_MD_CTX*
  void *hmac_key_data_;
  size_t hmac_key_size_;
  bool keyed_mode_ = false;
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_OPENSSL_SHA_HASHER_H
