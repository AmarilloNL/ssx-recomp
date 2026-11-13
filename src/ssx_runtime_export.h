// ssx - look up the runtime library's own definition of a guest import.
//
// Defining an __imp__X symbol in the exe shadows the runtime's export for the
// recompiled code; wrappers use this to forward to the original.

#pragma once

#include <cstdint>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <rex/ppc.h>

using GuestFunc = void (*)(PPCContext&, uint8_t*);

inline GuestFunc ResolveRuntimeExport(const char* name) {
#ifdef _WIN32
  for (const char* dll : {"rexruntime.dll", "rexruntimerd.dll", "rexruntimed.dll"}) {
    if (HMODULE module = GetModuleHandleA(dll)) {
      if (FARPROC proc = GetProcAddress(module, name)) {
        return reinterpret_cast<GuestFunc>(proc);
      }
    }
  }
#else
  // The next definition after the exe's own: librexruntime.so's.
  if (void* proc = dlsym(RTLD_NEXT, name)) return reinterpret_cast<GuestFunc>(proc);
#endif
  std::abort();
}
