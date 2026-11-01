// ssx - look up the runtime DLL's own definition of a guest import.
//
// Defining an __imp__X symbol in the exe shadows the runtime's export for the
// recompiled code; wrappers use this to forward to the original.

#pragma once

#include <cstdint>
#include <cstdlib>

#include <windows.h>

#include <rex/ppc.h>

using GuestFunc = void (*)(PPCContext&, uint8_t*);

inline GuestFunc ResolveRuntimeExport(const char* name) {
  for (const char* dll : {"rexruntime.dll", "rexruntimerd.dll", "rexruntimed.dll"}) {
    if (HMODULE module = GetModuleHandleA(dll)) {
      if (FARPROC proc = GetProcAddress(module, name)) {
        return reinterpret_cast<GuestFunc>(proc);
      }
    }
  }
  std::abort();
}
