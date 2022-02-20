//===- ObjCARCAnalysisUtils.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements common infrastructure for libLLVMObjCARCOpts.a, which
// implements several scalar transformations over the LLVM intermediate
// representation, including the C bindings for that library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace llvm::objcarc;

/// A handy option to enable/disable all ARC Optimizations.
bool llvm::objcarc::EnableARCOpts;
static cl::opt<bool, true> EnableARCOptimizations(
    "enable-objc-arc-opts", cl::desc("enable/disable all ARC Optimizations"),
    cl::location(EnableARCOpts), cl::init(true), cl::Hidden);

const Value *llvm::objcarc::GetUnderlyingObjCPtr(const Value *V) {
  for (;;) {
    V = getUnderlyingObject(V);
    if (!IsForwarding(GetBasicARCInstKind(V)))
      break;
    V = cast<CallInst>(V)->getArgOperand(0);
  }

  return V;
}

const Value *llvm::objcarc::GetRCIdentityRoot(const Value *V) {
  for (;;) {
    V = V->stripPointerCasts();
    if (!IsForwarding(GetBasicARCInstKind(V)))
      break;
    V = cast<CallInst>(V)->getArgOperand(0);
  }
  return V;
}

Value *llvm::objcarc::GetArgRCIdentityRoot(Value *Inst) {
  return GetRCIdentityRoot(cast<CallInst>(Inst)->getArgOperand(0));
}

bool llvm::objcarc::IsNoopInstruction(const Instruction *I) {
  return isa<BitCastInst>(I) ||
         (isa<GetElementPtrInst>(I) &&
          cast<GetElementPtrInst>(I)->hasAllZeroIndices());
}

bool llvm::objcarc::IsPotentialRetainableObjPtr(const Value *Op) {
  // Pointers to static or stack storage are not valid retainable object
  // pointers.
  if (isa<Constant>(Op) || isa<AllocaInst>(Op))
    return false;
  // Special arguments can not be a valid retainable object pointer.
  if (const Argument *Arg = dyn_cast<Argument>(Op))
    if (Arg->hasPassPointeeByValueCopyAttr() || Arg->hasNestAttr() ||
        Arg->hasStructRetAttr())
      return false;
  // Only consider values with pointer types.
  //
  // It seemes intuitive to exclude function pointer types as well, since
  // functions are never retainable object pointers, however clang occasionally
  // bitcasts retainable object pointers to function-pointer type temporarily.
  PointerType *Ty = dyn_cast<PointerType>(Op->getType());
  if (!Ty)
    return false;
  // Conservatively assume anything else is a potential retainable object
  // pointer.
  return true;
}

bool llvm::objcarc::IsPotentialRetainableObjPtr(const Value *Op,
                                                AAResults &AA) {
  // First make the rudimentary check.
  if (!IsPotentialRetainableObjPtr(Op))
    return false;

  // Objects in constant memory are not reference-counted.
  if (AA.pointsToConstantMemory(Op))
    return false;

  // Pointers in constant memory are not pointing to reference-counted objects.
  if (const LoadInst *LI = dyn_cast<LoadInst>(Op))
    if (AA.pointsToConstantMemory(LI->getPointerOperand()))
      return false;

  // Otherwise assume the worst.
  return true;
}

bool llvm::objcarc::IsObjCIdentifiedObject(const Value *V) {
  // Assume that call results and arguments have their own "provenance".
  // Constants (including GlobalVariables) and Allocas are never
  // reference-counted.
  if (isa<CallInst>(V) || isa<InvokeInst>(V) || isa<Argument>(V) ||
      isa<Constant>(V) || isa<AllocaInst>(V))
    return true;

  if (const LoadInst *LI = dyn_cast<LoadInst>(V)) {
    const Value *Pointer = GetRCIdentityRoot(LI->getPointerOperand());
    if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(Pointer)) {
      // A constant pointer can't be pointing to an object on the heap. It may
      // be reference-counted, but it won't be deleted.
      if (GV->isConstant())
        return true;
      StringRef Name = GV->getName();
      // These special variables are known to hold values which are not
      // reference-counted pointers.
      if (Name.startswith("\01l_objc_msgSend_fixup_"))
        return true;

      StringRef Section = GV->getSection();
      if (Section.contains("__message_refs") ||
          Section.contains("__objc_classrefs") ||
          Section.contains("__objc_superrefs") ||
          Section.contains("__objc_methname") || Section.contains("__cstring"))
        return true;
    }
  }

  return false;
}
