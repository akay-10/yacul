// This translation unit provides explicit instantiations for the most common
// LockId specialisations so that the linker can share a single copy of the
// heavy template machinery across the binary, reducing code size.
//
// Projects that use exotic key types can still include lock_id.h directly;
// those types will be instantiated implicitly at the point of use.

#include "lock_id.h"

using namespace std;

namespace utils {
namespace concurrency {
// ---------------------------------------------------------------------------
// Explicit instantiations for frequently-used ID types
// ---------------------------------------------------------------------------

// int
template class LockId<int>;
template class LockIdGuard<int, hash<int>>;
template class LockIdSharedGuard<int, hash<int>>;

//------------------------------------------------------------------------
// long
template class LockId<long>;
template class LockIdGuard<long, hash<long>>;
template class LockIdSharedGuard<long, hash<long>>;

//------------------------------------------------------------------------
// unsigned long long  (database row-IDs, UUIDs packed as uint64)
template class LockId<unsigned long long>;
template class LockIdGuard<unsigned long long, hash<unsigned long long>>;
template class LockIdSharedGuard<unsigned long long, hash<unsigned long long>>;

//------------------------------------------------------------------------
// string  (service names, resource paths, etc.)
template class LockId<string>;
template class LockIdGuard<string, hash<string>>;
template class LockIdSharedGuard<string, hash<string>>;

// ---------------------------------------------------------------------------
// Explicit instantiations of TryLockFor / TryLockSharedFor
// (millisecond duration variants most common in production code)
// ---------------------------------------------------------------------------

// int + milliseconds
template LockIdGuard<int, hash<int>>
LockId<int>::TryLockFor(const int &,
                        const chrono::duration<long long, milli> &);
template LockIdGuard<int, hash<int>>
LockId<int>::TryLockFor(int &&, const chrono::duration<long long, milli> &);
template LockIdSharedGuard<int, hash<int>>
LockId<int>::TryLockSharedFor(const int &,
                              const chrono::duration<long long, milli> &);
template LockIdSharedGuard<int, hash<int>>
LockId<int>::TryLockSharedFor(int &&,
                              const chrono::duration<long long, milli> &);

//------------------------------------------------------------------------
// unsigned long long + milliseconds
template LockIdGuard<unsigned long long, hash<unsigned long long>>
LockId<unsigned long long>::TryLockFor(
    const unsigned long long &, const chrono::duration<long long, milli> &);
template LockIdGuard<unsigned long long, hash<unsigned long long>>
LockId<unsigned long long>::TryLockFor(
    unsigned long long &&, const chrono::duration<long long, milli> &);
template LockIdSharedGuard<unsigned long long, hash<unsigned long long>>
LockId<unsigned long long>::TryLockSharedFor(
    const unsigned long long &, const chrono::duration<long long, milli> &);
template LockIdSharedGuard<unsigned long long, hash<unsigned long long>>
LockId<unsigned long long>::TryLockSharedFor(
    unsigned long long &&, const chrono::duration<long long, milli> &);

//------------------------------------------------------------------------
// string + milliseconds
template LockIdGuard<string, hash<string>>
LockId<string>::TryLockFor(const string &,
                           const chrono::duration<long long, milli> &);
template LockIdGuard<string, hash<string>>
LockId<string>::TryLockFor(string &&,
                           const chrono::duration<long long, milli> &);
template LockIdSharedGuard<string, hash<string>>
LockId<string>::TryLockSharedFor(const string &,
                                 const chrono::duration<long long, milli> &);
template LockIdSharedGuard<string, hash<string>>
LockId<string>::TryLockSharedFor(string &&,
                                 const chrono::duration<long long, milli> &);
} // namespace concurrency
} // namespace utils
