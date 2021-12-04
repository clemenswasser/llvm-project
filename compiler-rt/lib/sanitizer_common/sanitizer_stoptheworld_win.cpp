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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <tlhelp32.h>

#include "sanitizer_stoptheworld.h"


namespace __sanitizer {

class SuspendedThreadsListWindows final : public SuspendedThreadsList {
 public:
  SuspendedThreadsListWindows() { thread_ids_.reserve(1024); }

  tid_t GetThreadID(uptr index) const override;
  uptr ThreadCount() const override;
  bool ContainsTid(tid_t thread_id) const;
  void Append(tid_t tid);

 private:
  InternalMmapVector<tid_t> thread_ids_;
};

tid_t SuspendedThreadsListWindows::GetThreadID(uptr index) const {
  CHECK_LT(index, thread_ids_.size());
  return thread_ids_[index];
}

uptr SuspendedThreadsListWindows::ThreadCount() const {
  return thread_ids_.size();
}

bool SuspendedThreadsListWindows::ContainsTid(tid_t thread_id) const {
  for (uptr i = 0; i < thread_ids_.size(); i++) {
    if (thread_ids_[i] == thread_id)
      return true;
  }
  return false;
}

void SuspendedThreadsListWindows::Append(tid_t tid) {
  thread_ids_.push_back(tid);
}

void StopTheWorld(StopTheWorldCallback callback, void *argument) {
  const HANDLE threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

  CHECK(threads != INVALID_HANDLE_VALUE);

  const DWORD this_thread = GetCurrentThreadId();
  const DWORD this_process = GetCurrentProcessId();

  SuspendedThreadsListWindows suspended_threads_list;
  InternalMmapVector<HANDLE> suspended_threads_handles;

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

        const DWORD thread_id = GetThreadId(thread);
        CHECK(thread_id);
        suspended_threads_list.Append(thread_id);
      }
      thread_entry.dwSize = sizeof(thread_entry);
    } while (Thread32Next(threads, &thread_entry));
  }

  callback(suspended_threads_list, argument);

  for(const HANDLE suspended_thread_handle : suspended_threads_handles) {
    ResumeThread(suspended_thread_handle);
    CloseHandle(suspended_thread_handle);
  }

  CloseHandle(threads);
}

}  // namespace __sanitizer

#endif  // SANITIZER_WINDOWS
