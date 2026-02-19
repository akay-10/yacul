#ifndef UTILS_SUBUTIL_FILE_H 
#define UTILS_SUBUTIL_FILE_H

#include <memory> // shared_ptr

#include "basic/basic.h"

namespace utils { namespace snippet {

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

}} // namespace utils::snippet

#endif // UTILS_SUBUTIL_FILE_H

