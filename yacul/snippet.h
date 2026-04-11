#ifndef UTILS_SUBUTIL_FILE_H
#define UTILS_SUBUTIL_FILE_H

#include "yacul/basic/basic.h"

#include <memory> // shared_ptr

namespace utils {
namespace snippet {

class Snippet {
public:
  typedef std::shared_ptr<Snippet> Ptr;
  typedef std::shared_ptr<const Snippet> PtrConst;

  Snippet();
  ~Snippet();

private:
private:
  DISALLOW_COPY_AND_ASSIGN(Snippet);
};

} // namespace snippet
} // namespace utils

#endif // UTILS_SUBUTIL_FILE_H
