#ifndef UTILS_HASHING_HASH_H
#define UTILS_HASHING_HASH_H

#include "absl/hash/hash.h"
#include "yacul/hashing/os_murmur_hash3.h"

namespace utils {
namespace hashing {

enum class HashType {
  kStdHash = 0,
  kMurmurHashx86_32 = 1,
  kAbseilHash = 2,
  kNumHashType = 3
};

namespace detail {

template <typename T> struct is_pair : std::false_type {};

template <typename T1, typename T2>
struct is_pair<std::pair<T1, T2>> : std::true_type {};

template <typename T> struct is_tuple : std::false_type {};

template <typename... Args>
struct is_tuple<std::tuple<Args...>> : std::true_type {};

template <typename> inline constexpr bool always_false = false;

} // namespace detail

template <typename T, HashType HType = HashType::kStdHash> class Hash {
public:
  uint32_t operator()(const T &key, const uint32_t seed = 1234) const {
    return HashHelper(key, seed);
  }

private:
  // Strip const/volatile from T for type comparisons and hash operations
  using CleanT = std::remove_cv_t<T>;

  uint32_t HashHelper(const T &key, const uint32_t seed) const {
    if constexpr (std::is_same_v<CleanT, std::string>) {
      return HashString(key, seed);
    } else if constexpr (std::is_same_v<CleanT, const char *> ||
                         std::is_same_v<CleanT, char *>) {
      return HashCString(key, seed);
    } else if constexpr (std::is_integral_v<CleanT>) {
      return HashInt(key, seed);
    } else if constexpr (detail::is_pair<CleanT>::value) {
      return HashPair(key, seed);
    } else if constexpr (detail::is_tuple<CleanT>::value) {
      return HashTuple(key, seed);
    } else {
      // Fallback to absl::hash for unsupported types. This is not ideal though.
      return absl::Hash<T>{}(key);
    }
  }

  uint32_t HashString(const T &key, const uint32_t seed) const {
    static_assert(std::is_same_v<CleanT, std::string>,
                  "Expected T to be std::string");
    uint32_t hash = 0;
    if constexpr (HType == HashType::kMurmurHashx86_32) {
      MurmurHash3_x86_32(key.c_str(), key.size(), seed, &hash);
    } else if constexpr (HType == HashType::kAbseilHash) {
      hash = absl::Hash<CleanT>{}(key);
    } else {
      hash = std::hash<CleanT>{}(key);
    }
    return hash;
  }

  uint32_t HashCString(const T &key, const uint32_t seed) const {
    static_assert(std::is_same_v<CleanT, const char *> ||
                    std::is_same_v<CleanT, char *>,
                  "Expected T to be c string");
    uint32_t hash = 0;
    if constexpr (HType == HashType::kMurmurHashx86_32) {
      MurmurHash3_x86_32(key, std::strlen(key), seed, &hash);
    } else if constexpr (HType == HashType::kAbseilHash) {
      std::string key_str(key, std::strlen(key));
      hash = absl::Hash<std::string>{}(key_str);
    } else {
      std::string key_str(key, std::strlen(key));
      hash = std::hash<std::string>{}(key_str);
    }
    return hash;
  }

  uint32_t HashInt(const T &key, const uint32_t seed) const {
    static_assert(std::is_integral_v<CleanT>, "Expected T to be an integer");
    uint32_t hash = 0;
    if constexpr (HType == HashType::kMurmurHashx86_32) {
      MurmurHash3_x86_32(&key, sizeof(CleanT), seed, &hash);
    } else if constexpr (HType == HashType::kAbseilHash) {
      hash = absl::Hash<CleanT>{}(key);
    } else {
      hash = std::hash<CleanT>{}(key);
    }
    return hash;
  }

  template <typename EleT>
  uint32_t HashElement(const EleT &ele, const uint32_t seed) const {
    using CleanEleT = std::remove_cv_t<EleT>;
    uint32_t hash = 0;
    if constexpr (std::is_integral_v<CleanEleT>) {
      Hash<CleanEleT, HType> ele_int_hasher;
      hash = ele_int_hasher(ele, seed);
    } else if constexpr (std::is_same_v<CleanEleT, std::basic_string<char>> ||
                         std::is_same_v<CleanEleT, char *>) {
      Hash<std::string, HType> ele_string_hasher;
      hash = ele_string_hasher(ele, seed);
    } else {
      static_assert(detail::always_false<EleT>, "Unreachable");
    }
    return hash;
  }

  uint32_t HashPair(const T &key, const uint32_t seed) const {
    static_assert(detail::is_pair<CleanT>::value,
                  "Expected T to be a std::pair");
    uint32_t hash = 0;
    if constexpr (HType == HashType::kMurmurHashx86_32) {
      hash = HashElement(key.first, seed);
      hash = HashElement(key.second, hash);
    } else if constexpr (HType == HashType::kAbseilHash) {
      hash = absl::Hash<CleanT>{}(key);
    } else {
      static_assert(detail::always_false<T>,
                    "Unsupported stdd::pair type for std::hash");
    }
    return hash;
  }

  uint32_t HashTuple(const T &key, const uint32_t seed) const {
    static_assert(detail::is_tuple<CleanT>::value,
                  "Expected T to be a std::tuple");
    uint32_t hash = seed;
    if constexpr (HType == HashType::kMurmurHashx86_32) {
      std::apply([this, &hash](
                   auto &&...args) { ((hash = HashElement(args, hash)), ...); },
                 key);
    } else if constexpr (HType == HashType::kAbseilHash) {
      hash = absl::Hash<CleanT>{}(key);
    } else {
      static_assert(detail::always_false<T>,
                    "Unsupported std::tuple type for std::hash");
    }
    return hash;
  }
};

} // namespace hashing
} // namespace utils

#endif // UTILS_HASHING_HASH_H
