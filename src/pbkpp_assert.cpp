#include "pbkpp_assert.h"

#ifndef NDEBUG

#include <hal/debug.h>
#include <pbkit/pbkit.h>
#include <windows.h>

#include <cstdio>

namespace PBKitPlusPlus {

[[noreturn]] void PBKPP_PrintAssertAndWaitForever(const char *assert_code, const char *filename, uint32_t line) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "ASSERT FAILED: '%s' at %s:%d\n", assert_code, filename, line);
  OutputDebugString(buffer);
  debugPrint("ASSERT FAILED!\n-=[\n\n%s\n\n]=-\nat %s:%d\n", assert_code, filename, line);
  debugPrint("\nHalted, please reboot.\n");
  pb_show_debug_screen();
  while (true) {
    Sleep(2000);
  }
}

}  // namespace PBKitPlusPlus
#endif
