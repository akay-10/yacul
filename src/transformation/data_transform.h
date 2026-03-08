#ifndef UTILS_TRANSFORMATION_DATA_TRANSFORM_H
#define UTILS_TRANSFORMATION_DATA_TRANSFORM_H
// ---------------------------------------------------------------------------
// Convenience umbrella header — include this to get all public API types.
// ---------------------------------------------------------------------------

#include "i_data_transformer.h"
#include "transformer_factory.h"

#if defined(UTILS_TRANSFORMATION_HAS_LZ4)
#include "lz4_compressor.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_ZSTD)
#include "zstd_compressor.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_SNAPPY)
#include "snappy_compressor.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_XXHASH)
#include "xxhash_checksummer.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_CRC32C)
#include "crc32c_checksummer.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_BLAKE3)
#include "blake3_hasher.h"
#endif

#if defined(UTILS_TRANSFORMATION_HAS_OPENSSL)
#include "openssl_sha_hasher.h"
#endif

#endif // UTILS_TRANSFORMATION_DATA_TRANSFORM_H
