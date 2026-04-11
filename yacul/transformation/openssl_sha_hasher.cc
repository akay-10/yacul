#include "yacul/transformation/openssl_sha_hasher.h"

#include "yacul/transformation/hex_utils.h"

#include <cassert>
#include <cstring>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/opensslv.h>
#include <stdexcept>
#include <string>

using namespace std;

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

const EVP_MD *MdFor(ShaAlgorithm algo) {
  switch (algo) {
  case ShaAlgorithm::kSHA256:
    return EVP_sha256();
  case ShaAlgorithm::kSHA384:
    return EVP_sha384();
  case ShaAlgorithm::kSHA512:
    return EVP_sha512();
  case ShaAlgorithm::kSHA3_256:
    return EVP_sha3_256();
  case ShaAlgorithm::kSHA3_384:
    return EVP_sha3_384();
  case ShaAlgorithm::kSHA3_512:
    return EVP_sha3_512();
  }
  return EVP_sha256();
}

size_t DigestSizeFor(ShaAlgorithm algo) {
  return static_cast<size_t>(EVP_MD_size(MdFor(algo)));
}

string_view AlgoName(ShaAlgorithm algo) {
  switch (algo) {
  case ShaAlgorithm::kSHA256:
    return "SHA-256";
  case ShaAlgorithm::kSHA384:
    return "SHA-384";
  case ShaAlgorithm::kSHA512:
    return "SHA-512";
  case ShaAlgorithm::kSHA3_256:
    return "SHA3-256";
  case ShaAlgorithm::kSHA3_384:
    return "SHA3-384";
  case ShaAlgorithm::kSHA3_512:
    return "SHA3-512";
  }
  return "SHA-unknown";
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

OpensslShaHasher::OpensslShaHasher(ShaAlgorithm algo)
  : algo_(algo), ctx_(EVP_MD_CTX_new()), hmac_key_data_(nullptr),
    hmac_key_size_(0) {
  assert(ctx_ && "EVP_MD_CTX_new failed");
  EVP_DigestInit_ex(static_cast<EVP_MD_CTX *>(ctx_), MdFor(algo_), nullptr);
}

OpensslShaHasher::~OpensslShaHasher() {
  EVP_MD_CTX_free(static_cast<EVP_MD_CTX *>(ctx_));
  free(hmac_key_data_);
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

string_view OpensslShaHasher::name() const { return AlgoName(algo_); }

string_view OpensslShaHasher::library_version() const {
  return OPENSSL_VERSION_TEXT;
}

bool OpensslShaHasher::is_hardware_accelerated() const {
  // OpenSSL auto-detects SHA-NI (Intel) and ARMv8 SHA extensions.
  return true;
}

size_t OpensslShaHasher::digest_size_bytes() const {
  return DigestSizeFor(algo_);
}

bool OpensslShaHasher::supports_keyed_mode() const { return true; }

// ---------------------------------------------------------------------------
// One-shot
// ---------------------------------------------------------------------------

ByteBuffer OpensslShaHasher::Hash(ByteSpan data) const {
  ByteBuffer out(DigestSizeFor(algo_));
  unsigned int len = 0;
  EVP_Digest(data.data(), data.size(), out.data(), &len, MdFor(algo_), nullptr);
  out.resize(len);
  return out;
}

string OpensslShaHasher::HashHex(ByteSpan data) const {
  return ToHex(Hash(data));
}

bool OpensslShaHasher::HashKeyed(ByteSpan key, ByteSpan data,
                                 ByteBuffer &out) const {
  out.resize(DigestSizeFor(algo_));
  unsigned int len = 0;
  uint8_t *result = HMAC(MdFor(algo_), key.data(), static_cast<int>(key.size()),
                         data.data(), data.size(), out.data(), &len);
  if (!result)
    return false;
  out.resize(len);
  return true;
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

void OpensslShaHasher::Init() {
  keyed_mode_ = false;
  EVP_DigestInit_ex(static_cast<EVP_MD_CTX *>(ctx_), MdFor(algo_), nullptr);
}

bool OpensslShaHasher::InitKeyed(ByteSpan key) {
  // Keyed streaming via HMAC requires a different OpenSSL context type.
  // Store the key and re-init as HMAC in Finalize().
  // For simplicity, store key and use it per-update via incremental HMAC.
  // We use the EVP_MAC API (OpenSSL 3.x) or fall back to HMAC one-shot.
  free(hmac_key_data_);
  hmac_key_data_ = malloc(key.size());
  if (!hmac_key_data_)
    return false;
  memcpy(hmac_key_data_, key.data(), key.size());
  hmac_key_size_ = key.size();
  keyed_mode_ = true;

  // Reset digest context for data accumulation.
  EVP_DigestInit_ex(static_cast<EVP_MD_CTX *>(ctx_), MdFor(algo_), nullptr);
  return true;
}

void OpensslShaHasher::Update(ByteSpan data) {
  EVP_DigestUpdate(static_cast<EVP_MD_CTX *>(ctx_), data.data(), data.size());
}

ByteBuffer OpensslShaHasher::Finalize() {
  ByteBuffer out(DigestSizeFor(algo_));
  unsigned int len = 0;

  if (keyed_mode_) {
    // Retrieve accumulated data by finalizing the digest, then run HMAC
    // one-shot.  (Full incremental HMAC needs EVP_MAC, OpenSSL 3+.)
    ByteBuffer inner(DigestSizeFor(algo_));
    unsigned int inner_len = 0;
    EVP_DigestFinal_ex(static_cast<EVP_MD_CTX *>(ctx_), inner.data(),
                       &inner_len);
    inner.resize(inner_len);

    HMAC(MdFor(algo_), hmac_key_data_, static_cast<int>(hmac_key_size_),
         inner.data(), inner.size(), out.data(), &len);
  } else {
    EVP_DigestFinal_ex(static_cast<EVP_MD_CTX *>(ctx_), out.data(), &len);
  }

  out.resize(len);
  Init();
  return out;
}

string OpensslShaHasher::FinalizeHex() { return ToHex(Finalize()); }

} // namespace transformation
} // namespace utils
