#ifndef UTILS_BASIC_BASIC_H
#define UTILS_BASIC_BASIC_H

//------------------------------------------------------------------------------
// Most used headers
//------------------------------------------------------------------------------
#include <iostream> // For cout, cin, endl
#include <memory> // For shared_ptr, unique_ptr
#include <cstring> // For strlen
#include <unordered_map> // For unordered_map
#include <unordered_set> // For unordered_set
#include <vector> // For vector
#include <string> // For string
#include <utility> // For pair, make_pair
#include <tuple> // For tuple, make_tuple
#include <type_traits> // For type traits like is_integral, is_same
#include <cstdint> // For fixed-width integer types like uint32_t
#include <cassert> // For assert
#include <functional> // For std::hash

//------------------------------------------------------------------------------
// Common class macros
//------------------------------------------------------------------------------
#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                     \
  TypeName(const TypeName &) = delete;                                         \
  const TypeName &operator=(const TypeName &) = delete;

#define DISALLOW_CONST_RVALUE_COPY_AND_ASSIGN(TypeName)                        \
  TypeName(const TypeName &&) = delete;                                        \
  void operator=(const TypeName &&) = delete;

//------------------------------------------------------------------------------
// Defining LOGVARS(...) for logging variable names and values
//------------------------------------------------------------------------------
template <typename T> struct NamedVar {
  const char *name;
  const T &value;

  friend std::ostream &operator<<(std::ostream &os, const NamedVar &nv) {
    return os << nv.name << "=" << nv.value;
  }
};

// Primary template declaration (variadic)
template <typename... Args> class VarPrinter;

// Base case: single variable
template <typename T> class VarPrinter<T> {
  NamedVar<T> var;

public:
  VarPrinter(NamedVar<T> v) : var(v) {}

  friend std::ostream &operator<<(std::ostream &os, const VarPrinter &vp) {
    return os << vp.var;
  }
};

// Recursive case: multiple variables
template <typename T, typename... Rest> class VarPrinter<T, Rest...> {
  NamedVar<T> var;
  VarPrinter<Rest...> rest;

public:
  VarPrinter(NamedVar<T> v, NamedVar<Rest>... r) : var(v), rest(r...) {}

  friend std::ostream &operator<<(std::ostream &os, const VarPrinter &vp) {
    return os << vp.var << "," << vp.rest;
  }
};

// Helper function to deduce template arguments
template <typename... Args>
VarPrinter<Args...> make_printer(NamedVar<Args>... vars) {
  return VarPrinter<Args...>(vars...);
}

#define LOGVARS(...) make_printer(LOGVARS_IMPL(__VA_ARGS__))
#define LOGVARS_IMPL(...) LOGVARS_FOR_EACH(LOGVARS_PAIR, __VA_ARGS__)

#define LOGVARS_PAIR(x)                                                        \
  NamedVar<decltype(x)> { #x, x }

#define LOGVARS_FOR_EACH(macro, ...)                                           \
  LOGVARS_EXPAND(LOGVARS_CHOOSE(__VA_ARGS__)(macro, __VA_ARGS__))

#define LOGVARS_EXPAND(...) __VA_ARGS__

#define LOGVARS_CHOOSE(...)                                                    \
  LOGVARS_GET(__VA_ARGS__, LOGVARS_8, LOGVARS_7, LOGVARS_6, LOGVARS_5,         \
              LOGVARS_4, LOGVARS_3, LOGVARS_2, LOGVARS_1, )

#define LOGVARS_GET(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N

#define LOGVARS_1(m, x) m(x)
#define LOGVARS_2(m, x, ...) m(x), LOGVARS_1(m, __VA_ARGS__)
#define LOGVARS_3(m, x, ...) m(x), LOGVARS_2(m, __VA_ARGS__)
#define LOGVARS_4(m, x, ...) m(x), LOGVARS_3(m, __VA_ARGS__)
#define LOGVARS_5(m, x, ...) m(x), LOGVARS_4(m, __VA_ARGS__)
#define LOGVARS_6(m, x, ...) m(x), LOGVARS_5(m, __VA_ARGS__)
#define LOGVARS_7(m, x, ...) m(x), LOGVARS_6(m, __VA_ARGS__)
#define LOGVARS_8(m, x, ...) m(x), LOGVARS_7(m, __VA_ARGS__)

//------------------------------------------------------------------------------
// Math functions
//------------------------------------------------------------------------------
template <typename T> inline T CeilDiv(T a, T b) {
  return (a + b - 1) / b;
}

template <typename T> inline T RoundUp(T a, T b) {
  return CeilDiv(a, b) * b;
}

template <typename T> inline T RoundDown(T a, T b) {
  return (a / b) * b;
}

template <typename T> inline T IsPowerOfTwo(T x) {
  return x > 0 && (x & (x - 1)) == 0;
}

template <typename T> inline T NextPowerOfTwo(T x) {
  if (x <= 1) return 1;
  --x;
  for (size_t i = 1; i < sizeof(T) * 8; i <<= 1) {
    x |= x >> i;
  }
  return x + 1;
}

template <typename T> inline T PrevPowerOfTwo(T x) {
  if (x <= 1) return 0;
  T power = 1;
  while (power <= x) {
    power <<= 1;
  }
  return power >> 1;
}

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------
// Cache line size for most modern x86-64 processors
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

#endif // UTILS_BASIC_BASIC_H

