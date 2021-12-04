//===-- sanitizer_stoptheworld_win.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See sanitizer_stoptheworld.h for details.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_platform.h"

#if SANITIZER_WINDOWS

#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

// windows.h needs to be included before tlhelp32.h
#  include <tlhelp32.h>

#  include "sanitizer_stoptheworld.h"

namespace __sanitizer {

struct SuspendedThreadsListWindows final : public SuspendedThreadsList {
  InternalMmapVector<HANDLE> threadHandles;
  InternalMmapVector<DWORD> threadIds;

  SuspendedThreadsListWindows() {
    threadIds.reserve(1024);
    threadHandles.reserve(1024);
  }

  PtraceRegistersStatus GetRegistersAndSP(uptr index,
                                          InternalMmapVector<uptr> *buffer,
                                          uptr *sp) const override;

  tid_t GetThreadID(uptr index) const override;
  uptr ThreadCount() const override;
};

PtraceRegistersStatus SuspendedThreadsListWindows::GetRegistersAndSP(
    uptr index, InternalMmapVector<uptr> *buffer, uptr *sp) const {
  CHECK_LT(index, threadHandles.size());

  CONTEXT thread_context;
  thread_context.ContextFlags = CONTEXT_ALL;
  CHECK(GetThreadContext(threadHandles[index], &thread_context));

  buffer->resize(RoundUpTo(sizeof(thread_context), sizeof(uptr)) /
                 sizeof(uptr));
  internal_memcpy(buffer->data(), &thread_context, sizeof(thread_context));

  *sp = thread_context.Rsp;

  return REGISTERS_AVAILABLE;
}

tid_t SuspendedThreadsListWindows::GetThreadID(uptr index) const {
  CHECK_LT(index, threadIds.size());
  return threadIds[index];
}

uptr SuspendedThreadsListWindows::ThreadCount() const {
  return threadIds.size();
}

struct RunThreadArgs {
  StopTheWorldCallback callback;
  void *argument;
};

DWORD WINAPI RunThread(void *argument) {
  RunThreadArgs *run_args = (RunThreadArgs *)argument;
  const HANDLE threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

  CHECK(threads != INVALID_HANDLE_VALUE);

  const DWORD this_thread = GetCurrentThreadId();
  const DWORD this_process = GetCurrentProcessId();

  SuspendedThreadsListWindows suspended_threads_list;

  THREADENTRY32 thread_entry;
  thread_entry.dwSize = sizeof(thread_entry);
  if (Thread32First(threads, &thread_entry)) {
    do {
      if (thread_entry.dwSize >=
          FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) +
              sizeof(thread_entry.th32OwnerProcessID)) {
        if (thread_entry.th32ThreadID == this_thread ||
            thread_entry.th32OwnerProcessID != this_process)
          continue;

        const HANDLE thread =
            OpenThread(THREAD_ALL_ACCESS, FALSE, thread_entry.th32ThreadID);
        CHECK(thread);
        SuspendThread(thread);

        suspended_threads_list.threadIds.push_back(thread_entry.th32ThreadID);
        suspended_threads_list.threadHandles.push_back(thread);
      }
      thread_entry.dwSize = sizeof(thread_entry);
    } while (Thread32Next(threads, &thread_entry));
  }

  run_args->callback(suspended_threads_list, run_args->argument);

  for (const auto suspended_thread_handle :
       suspended_threads_list.threadHandles) {
    ResumeThread(suspended_thread_handle);
    CloseHandle(suspended_thread_handle);
  }

  CloseHandle(threads);

  return 0;
}

void StopTheWorld(StopTheWorldCallback callback, void *argument) {
  struct RunThreadArgs arg = {callback, argument};
  DWORD trace_thread_id;

  auto trace_thread =
      CreateThread(nullptr, 0, RunThread, &arg, 0, &trace_thread_id);
  CHECK(trace_thread);

  WaitForSingleObject(trace_thread, INFINITE);
  CloseHandle(trace_thread);
}

}  // namespace __sanitizer

#endif  // SANITIZER_WINDOWS
