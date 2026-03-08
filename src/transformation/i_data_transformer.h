#ifndef UTILS_TRANSFORMATION_I_DATA_TRANSFORMER_H
#define UTILS_TRANSFORMATION_I_DATA_TRANSFORMER_H

// ---------------------------------------------------------------------------
// Pure abstract interface for all data transformation algorithms.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace utils {
namespace transformation {

// ---------------------------------------------------------------------------
// Common byte-buffer aliases
// ---------------------------------------------------------------------------
using ByteBuffer = std::vector<uint8_t>;
using ByteSpan = std::span<const uint8_t>;

// ---------------------------------------------------------------------------
// TransformCategory — broad class of the algorithm
// ---------------------------------------------------------------------------
enum class TransformCategory : uint8_t {
  kCompression = 0,
  kChecksum = 1,
  kHash = 2,
};

// ---------------------------------------------------------------------------
// IDataTransformer
//
// Root interface. Every concrete adapter must inherit from exactly one of
// the three sub-interfaces below. This root provides only identity metadata.
// ---------------------------------------------------------------------------
class IDataTransformer {
public:
  virtual ~IDataTransformer() = default;

  // Human-readable algorithm name, e.g. "LZ4", "xxHash-XXH3-128".
  virtual std::string_view name() const = 0;

  // Version string of the underlying library, e.g. "1.9.4".
  virtual std::string_view library_version() const = 0;

  // Category of this transformer.
  virtual TransformCategory category() const = 0;

  // Returns true if this implementation uses hardware acceleration
  // (e.g. SSE4.2, AVX2, SHA-NI, CRC32 instruction).
  virtual bool is_hardware_accelerated() const = 0;
};

// ---------------------------------------------------------------------------
// ICompressor
//
// Stateless, one-shot compression / decompression interface.
// Implementations MUST be thread-safe for concurrent calls to
// Compress() / Decompress() on the same object.
// ---------------------------------------------------------------------------
class ICompressor : public IDataTransformer {
public:
  ~ICompressor() override = default;

  TransformCategory category() const override {
    return TransformCategory::kCompression;
  }

  // -------------------------------------------------------------------------
  // Compress src into dst.
  // Returns true on success; dst contains the compressed bytes.
  // dst is resized/overwritten; caller need not pre-size it.
  // -------------------------------------------------------------------------
  virtual bool Compress(ByteSpan src, ByteBuffer &dst) const = 0;

  // -------------------------------------------------------------------------
  // Decompress src into dst.
  // original_size: hint for the expected decompressed size (0 = unknown).
  // Returns true on success.
  // -------------------------------------------------------------------------
  virtual bool Decompress(ByteSpan src, ByteBuffer &dst,
                          size_t original_size = 0) const = 0;

  // -------------------------------------------------------------------------
  // Upper-bound on compressed output for a given uncompressed size.
  // Used by callers who want to pre-allocate a buffer.
  // -------------------------------------------------------------------------
  virtual size_t MaxCompressedSize(size_t uncompressed_size) const = 0;

  // -------------------------------------------------------------------------
  // Speed / ratio trade-off level.
  // Range is implementation-defined; 0 means "use library default".
  // -------------------------------------------------------------------------
  virtual int compression_level() const = 0;
  virtual void set_compression_level(int level) = 0;
};

// ---------------------------------------------------------------------------
// IChecksummer
//
// Non-cryptographic, speed-first integrity check interface.
// Supports both one-shot and streaming (incremental) operation.
// ---------------------------------------------------------------------------
class IChecksummer : public IDataTransformer {
public:
  ~IChecksummer() override = default;

  TransformCategory category() const override {
    return TransformCategory::kChecksum;
  }

  // Width of the checksum in bytes (e.g. 4 for CRC32, 8 for XXH64).
  virtual size_t digest_size_bytes() const = 0;

  // -------------------------------------------------------------------------
  // One-shot: compute and return the checksum as a raw byte vector.
  // -------------------------------------------------------------------------
  virtual ByteBuffer Compute(ByteSpan data) const = 0;

  // -------------------------------------------------------------------------
  // One-shot convenience: return the checksum as a hex string.
  // -------------------------------------------------------------------------
  virtual std::string ComputeHex(ByteSpan data) const = 0;

  // -------------------------------------------------------------------------
  // Streaming / incremental interface.
  // Reset() clears any accumulated state.
  // Update() feeds more data.
  // Finalize() returns the final digest; streaming is reset after this call.
  // -------------------------------------------------------------------------
  virtual void Reset() = 0;
  virtual void Update(ByteSpan data) = 0;
  virtual ByteBuffer Finalize() = 0;
  virtual std::string FinalizeHex() = 0;
};

// ---------------------------------------------------------------------------
// IHasher
//
// Cryptographic (or crypto-strength) hash interface.
// Supports one-shot, streaming, and keyed-hash (MAC) modes.
// If a concrete implementation does not support keyed hashing,
// InitKeyed() should return false.
// ---------------------------------------------------------------------------
class IHasher : public IDataTransformer {
public:
  ~IHasher() override = default;

  TransformCategory category() const override {
    return TransformCategory::kHash;
  }

  // Output digest size in bytes (e.g. 32 for SHA-256, 64 for BLAKE3-512).
  virtual size_t digest_size_bytes() const = 0;

  // True if the algorithm supports keyed / MAC mode.
  virtual bool supports_keyed_mode() const = 0;

  // -------------------------------------------------------------------------
  // One-shot: hash data and return digest bytes.
  // -------------------------------------------------------------------------
  virtual ByteBuffer Hash(ByteSpan data) const = 0;

  // -------------------------------------------------------------------------
  // One-shot convenience: return digest as lowercase hex string.
  // -------------------------------------------------------------------------
  virtual std::string HashHex(ByteSpan data) const = 0;

  // -------------------------------------------------------------------------
  // Keyed one-shot (MAC).  Returns false if unsupported or key size invalid.
  // -------------------------------------------------------------------------
  virtual bool HashKeyed(ByteSpan key, ByteSpan data,
                         ByteBuffer &out) const = 0;

  // -------------------------------------------------------------------------
  // Streaming interface.
  // Init() resets to unkeyed mode.
  // InitKeyed() resets to keyed/MAC mode; returns false if unsupported.
  // Update() feeds more data; may be called any number of times.
  // Finalize() writes the digest into out and resets state.
  // -------------------------------------------------------------------------
  virtual void Init() = 0;
  virtual bool InitKeyed(ByteSpan key) = 0;
  virtual void Update(ByteSpan data) = 0;
  virtual ByteBuffer Finalize() = 0;
  virtual std::string FinalizeHex() = 0;
};

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_I_DATA_TRANSFORMER_H
