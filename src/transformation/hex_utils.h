#ifndef UTILS_TRANSFORMATION_HEX_UTILS_H
#define UTILS_TRANSFORMATION_HEX_UTILS_H

#include <cstdint>
#include <span>
#include <string>

namespace utils {
namespace transformation {

// Returns a lowercase hexadecimal representation of the given bytes.
inline std::string ToHex(std::span<const uint8_t> bytes) {
  static constexpr char kHexChars[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    out.push_back(kHexChars[(b >> 4) & 0xF]);
    out.push_back(kHexChars[b & 0xF]);
  }
  return out;
}

} // namespace transformation
} // namespace utils

#endif // UTILS_TRANSFORMATION_HEX_UTILS_H
