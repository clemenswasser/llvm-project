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

#include "sanitizer_common/sanitizer_platform.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "lsan_common.h"

#if CAN_SANITIZE_LEAKS && SANITIZER_WINDOWS

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "lsan_allocator.h"

namespace __lsan {

void HandleLeaks() {}

void InitializePlatformSpecificModules() {}

void LockStuffAndStopTheWorld(StopTheWorldCallback callback,
                              CheckForLeaksParam *argument) {
  LockThreadRegistry();
  LockAllocator();
  StopTheWorld(callback, argument);
  UnlockAllocator();
  UnlockThreadRegistry();
}

THREADLOCAL int disable_counter;
bool DisabledInThisThread() { return disable_counter > 0; }
void DisableInThisThread() { disable_counter++; }
void EnableInThisThread() {
  if (disable_counter == 0) {
    DisableCounterUnderflow();
  }
  disable_counter--;
}

void ProcessGlobalRegions(Frontier *frontier) {}
void ProcessPlatformSpecificAllocations(Frontier *frontier) {}

LoadedModule *GetLinker() { return nullptr; }

} // namespace __lsan

#endif // CAN_SANITIZE_LEAKS && SANITIZER_WINDOWS
