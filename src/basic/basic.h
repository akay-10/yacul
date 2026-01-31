#ifndef UTILS_BASIC_BASIC_H
#define UTILS_BASIC_BASIC_H

#include <iostream>

#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                     \
  TypeName(const TypeName &) = delete;                                         \
  const TypeName &operator=(const TypeName &) = delete;

#define DISALLOW_CONST_RVALUE_COPY_AND_ASSIGN(TypeName)                        \
  TypeName(const TypeName &&) = delete;                                        \
  void operator=(const TypeName &&) = delete;

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

#endif // UTILS_BASIC_BASIC_H

