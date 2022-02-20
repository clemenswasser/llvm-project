//===- ObjCARCAnalysisUtils.h - ObjC ARC Analysis Utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines common analysis utilities used by the ObjC ARC Optimizer.
/// ARC stands for Automatic Reference Counting and is a system for managing
/// reference counts for objects in Objective C.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OBJCARCANALYSISUTILS_H
#define LLVM_ANALYSIS_OBJCARCANALYSISUTILS_H

#include "llvm/ADT/Optional.h"
#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {

class AAResults;

namespace objcarc {

/// A handy option to enable/disable all ARC Optimizations.
extern bool EnableARCOpts;

/// Test if the given module looks interesting to run ARC optimization
/// on.
inline bool ModuleHasARC(const Module &M) {
  return
    M.getNamedValue("llvm.objc.retain") ||
    M.getNamedValue("llvm.objc.release") ||
    M.getNamedValue("llvm.objc.autorelease") ||
    M.getNamedValue("llvm.objc.retainAutoreleasedReturnValue") ||
    M.getNamedValue("llvm.objc.unsafeClaimAutoreleasedReturnValue") ||
    M.getNamedValue("llvm.objc.retainBlock") ||
    M.getNamedValue("llvm.objc.autoreleaseReturnValue") ||
    M.getNamedValue("llvm.objc.autoreleasePoolPush") ||
    M.getNamedValue("llvm.objc.loadWeakRetained") ||
    M.getNamedValue("llvm.objc.loadWeak") ||
    M.getNamedValue("llvm.objc.destroyWeak") ||
    M.getNamedValue("llvm.objc.storeWeak") ||
    M.getNamedValue("llvm.objc.initWeak") ||
    M.getNamedValue("llvm.objc.moveWeak") ||
    M.getNamedValue("llvm.objc.copyWeak") ||
    M.getNamedValue("llvm.objc.retainedObject") ||
    M.getNamedValue("llvm.objc.unretainedObject") ||
    M.getNamedValue("llvm.objc.unretainedPointer") ||
    M.getNamedValue("llvm.objc.clang.arc.use");
}

/// This is a wrapper around getUnderlyingObject which also knows how to
/// look through objc_retain and objc_autorelease calls, which we know to return
/// their argument verbatim.
const Value *GetUnderlyingObjCPtr(const Value *V);

/// A wrapper for GetUnderlyingObjCPtr used for results memoization.
inline const Value *GetUnderlyingObjCPtrCached(
    const Value *V,
    DenseMap<const Value *, std::pair<WeakVH, WeakTrackingVH>> &Cache) {
  // The entry is invalid if either value handle is null.
  auto InCache = Cache.lookup(V);
  if (InCache.first && InCache.second)
    return InCache.second;

  const Value *Computed = GetUnderlyingObjCPtr(V);
  Cache[V] =
      std::make_pair(const_cast<Value *>(V), const_cast<Value *>(Computed));
  return Computed;
}

/// The RCIdentity root of a value \p V is a dominating value U for which
/// retaining or releasing U is equivalent to retaining or releasing V. In other
/// words, ARC operations on \p V are equivalent to ARC operations on \p U.
///
/// We use this in the ARC optimizer to make it easier to match up ARC
/// operations by always mapping ARC operations to RCIdentityRoots instead of
/// pointers themselves.
///
/// The two ways that we see RCIdentical values in ObjC are via:
///
///   1. PointerCasts
///   2. Forwarding Calls that return their argument verbatim.
///
/// Thus this function strips off pointer casts and forwarding calls. *NOTE*
/// This implies that two RCIdentical values must alias.
const Value *GetRCIdentityRoot(const Value *V);

/// Helper which calls const Value *GetRCIdentityRoot(const Value *V) and just
/// casts away the const of the result. For documentation about what an
/// RCIdentityRoot (and by extension GetRCIdentityRoot is) look at that
/// function.
inline Value *GetRCIdentityRoot(Value *V) {
  return const_cast<Value *>(GetRCIdentityRoot((const Value *)V));
}

/// Assuming the given instruction is one of the special calls such as
/// objc_retain or objc_release, return the RCIdentity root of the argument of
/// the call.
Value *GetArgRCIdentityRoot(Value *Inst);

inline bool IsNullOrUndef(const Value *V) {
  return isa<ConstantPointerNull>(V) || isa<UndefValue>(V);
}

bool IsNoopInstruction(const Instruction *I);

/// Test whether the given value is possible a retainable object pointer.
bool IsPotentialRetainableObjPtr(const Value *Op);

bool IsPotentialRetainableObjPtr(const Value *Op, AAResults &AA);

/// Helper for GetARCInstKind. Determines what kind of construct CS
/// is.
inline ARCInstKind GetCallSiteClass(const CallBase &CB) {
  for (const Use &U : CB.args())
    if (IsPotentialRetainableObjPtr(U))
      return CB.onlyReadsMemory() ? ARCInstKind::User : ARCInstKind::CallOrUser;

  return CB.onlyReadsMemory() ? ARCInstKind::None : ARCInstKind::Call;
}

/// Return true if this value refers to a distinct and identifiable
/// object.
///
/// This is similar to AliasAnalysis's isIdentifiedObject, except that it uses
/// special knowledge of ObjC conventions.
bool IsObjCIdentifiedObject(const Value *V);

enum class ARCMDKindID {
  ImpreciseRelease,
  CopyOnEscape,
  NoObjCARCExceptions,
};

/// A cache of MDKinds used by various ARC optimizations.
class ARCMDKindCache {
  Module *M;

  /// The Metadata Kind for clang.imprecise_release metadata.
  llvm::Optional<unsigned> ImpreciseReleaseMDKind;

  /// The Metadata Kind for clang.arc.copy_on_escape metadata.
  llvm::Optional<unsigned> CopyOnEscapeMDKind;

  /// The Metadata Kind for clang.arc.no_objc_arc_exceptions metadata.
  llvm::Optional<unsigned> NoObjCARCExceptionsMDKind;

public:
  void init(Module *Mod) {
    M = Mod;
    ImpreciseReleaseMDKind = NoneType::None;
    CopyOnEscapeMDKind = NoneType::None;
    NoObjCARCExceptionsMDKind = NoneType::None;
  }

  unsigned get(ARCMDKindID ID) {
    switch (ID) {
    case ARCMDKindID::ImpreciseRelease:
      if (!ImpreciseReleaseMDKind)
        ImpreciseReleaseMDKind =
            M->getContext().getMDKindID("clang.imprecise_release");
      return *ImpreciseReleaseMDKind;
    case ARCMDKindID::CopyOnEscape:
      if (!CopyOnEscapeMDKind)
        CopyOnEscapeMDKind =
            M->getContext().getMDKindID("clang.arc.copy_on_escape");
      return *CopyOnEscapeMDKind;
    case ARCMDKindID::NoObjCARCExceptions:
      if (!NoObjCARCExceptionsMDKind)
        NoObjCARCExceptionsMDKind =
            M->getContext().getMDKindID("clang.arc.no_objc_arc_exceptions");
      return *NoObjCARCExceptionsMDKind;
    }
    llvm_unreachable("Covered switch isn't covered?!");
  }
};

} // end namespace objcarc
} // end namespace llvm

#endif
