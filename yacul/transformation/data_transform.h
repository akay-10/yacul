#ifndef UTILS_TRANSFORMATION_DATA_TRANSFORM_H
#define UTILS_TRANSFORMATION_DATA_TRANSFORM_H
// ---------------------------------------------------------------------------
// Convenience umbrella header — include this to get all public API types.
// ---------------------------------------------------------------------------

#include "yacul/transformation/i_data_transformer.h"
#include "yacul/transformation/transformer_factory.h"

#if defined(UTILS_TRANSFORMATION_HAS_LZ4)
#include "yacul/transformation/lz4_compressor.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_ZSTD)
#include "yacul/transformation/zstd_compressor.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_SNAPPY)
#include "yacul/transformation/snappy_compressor.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_XXHASH)
#include "yacul/transformation/xxhash_checksummer.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_CRC32C)
#include "yacul/transformation/crc32c_checksummer.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_BLAKE3)
#include "yacul/transformation/blake3_hasher.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
#include "yacul/transformation/openssl_sha_hasher.h"
#endif

#endif // UTILS_TRANSFORMATION_DATA_TRANSFORM_H
