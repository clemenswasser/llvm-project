//=-- lsan_win.h -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Standalone LSan RTL code common to Windows systems.
//
//===---------------------------------------------------------------------===//

#ifndef LSAN_WINDOWS_H
#define LSAN_WINDOWS_H

#include "lsan_thread.h"
#include "sanitizer_common/sanitizer_platform.h"

#if !SANITIZER_WINDOWS
#  error "lsan_win.h is used only on Windows systems (SANITIZER_WINDOWS)"
#endif

namespace __sanitizer {
struct DTLS;
}

namespace __lsan {

class ThreadContext final : public ThreadContextLsanBase {
 public:
  explicit ThreadContext(int tid);
  void OnStarted(void *arg) override;
};

void ThreadStart(u32 tid, tid_t os_id,
                 ThreadType thread_type = ThreadType::Regular);

}  // namespace __lsan

#endif  // LSAN_WINDOWS_H
