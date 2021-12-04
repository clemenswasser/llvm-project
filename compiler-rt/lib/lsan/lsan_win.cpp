//=-- lsan_win.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Standalone LSan RTL code common to POSIX-like systems.
//
//===---------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"

#if SANITIZER_WINDOWS
#include "lsan.h"
#include "lsan_allocator.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __lsan {

ThreadContext::ThreadContext(int tid) : ThreadContextLsanBase(tid) {}

struct OnStartedArgs {
  uptr stack_begin;
  uptr stack_end;
  uptr cache_begin;
  uptr cache_end;
  uptr tls_begin;
  uptr tls_end;
};

void ThreadContext::OnStarted(void *arg) {
  auto args = reinterpret_cast<const OnStartedArgs *>(arg);
  stack_begin_ = args->stack_begin;
  stack_end_ = args->stack_end;
  cache_begin_ = args->cache_begin;
  cache_end_ = args->cache_end;
}

void ThreadStart(u32 tid, tid_t os_id, ThreadType thread_type) {
  OnStartedArgs args;
  uptr stack_size = 0;
  uptr tls_size = 0;
  GetThreadStackAndTls(tid == kMainTid, &args.stack_begin, &stack_size,
                       &args.tls_begin, &tls_size);
  args.stack_end = args.stack_begin + stack_size;
  args.tls_end = args.tls_begin + tls_size;
  GetAllocatorCacheRange(&args.cache_begin, &args.cache_end);
  ThreadContextLsanBase::ThreadStart(tid, os_id, thread_type, &args);
}

bool GetThreadRangesLocked(tid_t os_id, uptr *stack_begin, uptr *stack_end,
                           uptr *tls_begin, uptr *tls_end, uptr *cache_begin,
                           uptr *cache_end, DTLS **dtls) {
  ThreadContext *context = static_cast<ThreadContext *>(
      GetThreadRegistryLocked()->FindThreadContextByOsIDLocked(os_id));
  if (!context)
    return false;
  *stack_begin = context->stack_begin();
  *stack_end = context->stack_end();
  *cache_begin = context->cache_begin();
  *cache_end = context->cache_end();
  *dtls = 0;
  return true;
}

void InitializeMainThread() {
  u32 tid = ThreadCreate(kMainTid, true);
  CHECK_EQ(tid, kMainTid);
  ThreadStart(tid, GetTid());
}

static void OnStackUnwind(const SignalContext &sig, const void *,
                          BufferedStackTrace *stack) {
  stack->Unwind(StackTrace::GetNextInstructionPc(sig.pc), sig.bp, sig.context,
                common_flags()->fast_unwind_on_fatal);
}

void LsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  HandleDeadlySignal(siginfo, context, GetCurrentThread(), &OnStackUnwind,
                     nullptr);
}

void ReplaceSystemMalloc() {}

static THREADLOCAL u32 current_thread_tid = kInvalidTid;
u32 GetCurrentThread() { return current_thread_tid; }
void SetCurrentThread(u32 tid) { current_thread_tid = tid; }

static THREADLOCAL AllocatorCache allocator_cache;
AllocatorCache *GetAllocatorCache() { return &allocator_cache; }

}  // namespace __lsan

int lsan_win_init() {
  __lsan_init();
  return 0;
}

#pragma section(".CRT$XIB", long, read)
__declspec(allocate(".CRT$XIB")) int (*__lsan_preinit)() = lsan_win_init;

#endif  // SANITIZER_WINDOWS
