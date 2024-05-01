//=-- lsan_common_win.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Implementation of common leak checking functionality. Darwin-specific code.
//
//===----------------------------------------------------------------------===//

#include "lsan_common.h"
#include "sanitizer_common/sanitizer_platform.h"

#if CAN_SANITIZE_LEAKS && SANITIZER_WINDOWS

#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>

// `windows.h` needs to be included before `dbghelp.h`
#  include <DbgHelp.h>
#  pragma comment(lib, "Dbghelp.lib")

namespace __lsan {

// TODO(cwasser): Intercepting `ExitProcess` doesn't currently work
void HandleLeaks() {
  //if (common_flags()->exitcode)
  //  Die();
}

void InitializePlatformSpecificModules() {}

void LockStuffAndStopTheWorld(StopTheWorldCallback callback,
                              CheckForLeaksParam *argument) {
  ScopedStopTheWorldLock lock;
  StopTheWorld(callback, argument);
}

THREADLOCAL int disable_counter = 0;
bool DisabledInThisThread() { return disable_counter > 0; }
void DisableInThisThread() { disable_counter++; }
void EnableInThisThread() {
  if (disable_counter == 0) {
    DisableCounterUnderflow();
  }
  disable_counter--;
}

BOOL CALLBACK EnumLoadedModulesCallback(PCSTR module_name, DWORD64 module_base,
                                        ULONG module_size, PVOID user_context) {
  auto *frontier = reinterpret_cast<Frontier *>(user_context);

  // Parse the PE Headers of all loaded Modules
  const auto *module_nt_header =
      ImageNtHeader(reinterpret_cast<PVOID>(module_base));

  CHECK(module_nt_header);

  const auto *section_header =
      reinterpret_cast<const IMAGE_SECTION_HEADER *>(module_nt_header + 1);

  // Find the `.data` section
  for (WORD i = 0; i < module_nt_header->FileHeader.NumberOfSections;
       ++i, ++section_header) {
    const char data_section_name[6] = ".data";
    const auto is_data_section =
        internal_strncmp(reinterpret_cast<const char *>(section_header->Name),
                         data_section_name, sizeof(data_section_name)) == 0;

    if (!is_data_section)
      continue;

    ScanGlobalRange(module_base + section_header->VirtualAddress,
                    module_base + section_header->VirtualAddress +
                        section_header->Misc.VirtualSize - 1,
                    frontier);
  }

  return TRUE;
}

void ProcessGlobalRegions(Frontier *frontier) {
  HANDLE this_process = GetCurrentProcess();

  EnumerateLoadedModules(this_process, EnumLoadedModulesCallback, frontier);
}
void ProcessPlatformSpecificAllocations(Frontier *frontier) {}

LoadedModule *GetLinker() { return nullptr; }

}  // namespace __lsan

#endif  // CAN_SANITIZE_LEAKS && SANITIZER_WINDOWS