#ifndef UTILS_HASHING_BLOOM_FILTER_H
#define UTILS_HASHING_BLOOM_FILTER_H

#include <memory> // shared_ptr

#include "basic/basic.h"

namespace utils {
namespace hashing {

class BloomFilter {
public:
  typedef std::shared_ptr<BloomFilter> Ptr;
  typedef std::shared_ptr<const BloomFilter> PtrConst;

  BloomFilter();
  ~BloomFilter();

private:
private:
  DISALLOW_COPY_AND_ASSIGN(BloomFilter);
};

} // namespace hashing
} // namespace utils

#endif // UTILS_HASHING_BLOOM_FILTER_H
