# yacul::transformation

Unified C++ interface for **compression**, **checksums**, and **cryptographic hashing** — targeting real-time and high-throughput applications.

## Library Matrix

| Category    | Algorithm          | Library                    | Throughput (approx.) |
|-------------|-------------------|----------------------------|-----------------------|
| Compression | LZ4               | [lz4/lz4][lz4]            | ~500–700 MB/s compress, 4 GB/s decompress |
| Compression | Zstd              | [facebook/zstd][zstd]     | ~300–500 MB/s @ level 1–3 |
| Compression | Snappy            | [google/snappy][snappy]   | ~250 MB/s compress, 500 MB/s decompress |
| Checksum    | xxHash XXH3-128   | [Cyan4973/xxHash][xxhash] | ~3–10 GB/s (AVX2) |
| Checksum    | CRC32C            | [google/crc32c][crc32c]   | ~13–20 GB/s (SSE4.2/ARMv8) |
| Hash        | BLAKE3            | [BLAKE3-team/BLAKE3][b3]  | ~2–4 GB/s (AVX2) |
| Hash        | SHA-256/384/512   | OpenSSL EVP + SHA-NI      | ~0.5–2 GB/s |
| Hash        | SHA-3 family      | OpenSSL EVP               | ~0.3–0.8 GB/s |

## Requirements

- C++20 compiler (GCC ≥ 11, Clang ≥ 13)
- OpenSSL development headers (for SHA adapter)

## CMake Options

| Option                          | Default | Description |
|---------------------------------|---------|-------------|
| UTILS_TRANSFORMATION_ENABLE_LZ4`      | ON      | LZ4 compression adapter |
| UTILS_TRANSFORMATION_ENABLE_ZSTD`     | ON      | Zstd compression adapter |
| UTILS_TRANSFORMATION_ENABLE_SNAPPY`   | ON      | Snappy compression adapter |
| UTILS_TRANSFORMATION_ENABLE_XXHASH`   | ON      | xxHash checksum adapter |
| UTILS_TRANSFORMATION_ENABLE_CRC32C`   | ON      | CRC32C checksum adapter |
| UTILS_TRANSFORMATION_ENABLE_BLAKE3`   | ON      | BLAKE3 hash adapter |
| UTILS_TRANSFORMATION_ENABLE_OPENSSL`  | ON      | OpenSSL SHA-2 / SHA-3 adapters |
| UTILS_BUILD_TEST`     | ON      | Build GTest suite |
| UTILS_BUILD_SHARED`    | OFF     | Build as shared library |

## Consuming via FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(yacul
  GIT_REPOSITORY https://github.com/akay-10/yacul.git
  GIT_TAG        master
)
set(UTILS_BUILD_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yacul)

target_link_libraries(your_target PRIVATE yacul::transformation)
```

## API Usage

### Via Factory (recommended)

```cpp
#include "yacul/transformation/data_transform.h"
using namespace utils::transformation;

// Compression
auto c = TransformerFactory::CreateCompressor(CompressorType::kLZ4);
ByteBuffer compressed, decompressed;
c->Compress(ByteSpan(input.data(), input.size()), compressed);
c->Decompress(ByteSpan(compressed.data(), compressed.size()),
              decompressed, input.size());

// Checksum
auto cs = TransformerFactory::CreateChecksummer(ChecksumType::kXXH3_128);
std::string hex = cs->ComputeHex(ByteSpan(data.data(), data.size()));

// Hash
auto h = TransformerFactory::CreateHasher(HashType::kBLAKE3);
ByteBuffer digest = h->Hash(ByteSpan(data.data(), data.size()));

// By name (runtime dispatch)
auto h2 = TransformerFactory::HasherByName("sha-256");
```

### Streaming (incremental)

```cpp
auto cs = TransformerFactory::CreateChecksummer(ChecksumType::kXXH3_128);
cs->Reset();
for (const auto& chunk : chunks) {
  cs->Update(ByteSpan(chunk.data(), chunk.size()));
}
std::string final_hex = cs->FinalizeHex();
```

### Keyed hash (MAC)

```cpp
auto h = TransformerFactory::CreateHasher(HashType::kBLAKE3);
const std::vector<uint8_t> key(32, 0xAB);  // Must be 32 bytes for BLAKE3
ByteBuffer mac;
h->HashKeyed(ByteSpan(key.data(), key.size()),
             ByteSpan(data.data(), data.size()),
             mac);
```

## Interface Hierarchy

```
IDataTransformer
├── ICompressor        (Compress / Decompress / MaxCompressedSize)
│   ├── Lz4Compressor
│   ├── ZstdCompressor
│   └── SnappyCompressor
├── IChecksummer       (Compute / ComputeHex / Reset / Update / Finalize)
│   ├── XxHashChecksummer   (XXH32 / XXH64 / XXH3-64 / XXH3-128)
│   └── Crc32cChecksummer
└── IHasher            (Hash / HashHex / HashKeyed / Init / Update / Finalize)
    ├── Blake3Hasher
    └── OpensslShaHasher    (SHA-256/384/512, SHA3-256/384/512)
```

[lz4]:    https://github.com/lz4/lz4
[zstd]:   https://github.com/facebook/zstd
[snappy]: https://github.com/google/snappy
[xxhash]: https://github.com/Cyan4973/xxHash
[crc32c]: https://github.com/google/crc32c
[b3]:     https://github.com/BLAKE3-team/BLAKE3
