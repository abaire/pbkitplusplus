#ifndef PBKITPLUSPLUS_SRC_PBKPP_ASSERT_H_
#define PBKITPLUSPLUS_SRC_PBKPP_ASSERT_H_

#ifndef NDEBUG
#include <cstdint>

#define PBKPP_ASSERT(c)                                                       \
  do {                                                                        \
    if (!(c)) {                                                               \
      PBKitPlusPlus::PBKPP_PrintAssertAndWaitForever(#c, __FILE__, __LINE__); \
    }                                                                         \
  } while (false)

namespace PBKitPlusPlus {
[[noreturn]] void PBKPP_PrintAssertAndWaitForever(const char *assert_code, const char *filename, uint32_t line);
}

#else

#define PBKPP_ASSERT(c) \
  do {                  \
  } while (false)

#endif  // #ifndef NDEBUG

#endif  // PBKITPLUSPLUS_SRC_PBKPP_ASSERT_H_
