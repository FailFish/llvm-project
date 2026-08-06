//===- SafeStack.cpp - Safe Stack Insertion -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass splits the stack into the safe stack (kept as-is for LLVM backend)
// and the unsafe stack (explicitly allocated and managed through the runtime
// support library).
//
// http://clang.llvm.org/docs/SafeStack.html
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/SafeStack.h"
#include "SafeStackLayout.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/StackLifetime.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

using namespace llvm;
using namespace llvm::safestack;

#define DEBUG_TYPE "safe-stack"

STATISTIC(NumFunctions, "Total number of functions");
STATISTIC(NumUnsafeStackFunctions, "Number of functions with unsafe stack");
STATISTIC(NumUnsafeStackRestorePointsFunctions,
          "Number of functions that use setjmp or exceptions");

STATISTIC(NumAllocas, "Total number of allocas");
STATISTIC(NumUnsafeStaticAllocas, "Number of unsafe static allocas");
STATISTIC(NumUnsafeDynamicAllocas, "Number of unsafe dynamic allocas");
STATISTIC(NumUnsafeByValArguments, "Number of unsafe byval arguments");
STATISTIC(NumUnsafeStackRestorePoints, "Number of setjmps and landingpads");

/// Use __safestack_pointer_address even if the platform has a faster way of
/// access safe stack pointer.
static cl::opt<bool>
    SafeStackUsePointerAddress("safestack-use-pointer-address",
                                  cl::init(false), cl::Hidden);

static cl::opt<bool> ClColoring("safe-stack-coloring",
                                cl::desc("enable safe stack coloring"),
                                cl::Hidden, cl::init(true));

/// Move every alloca to the unsafe stack, for stress-testing the layout,
/// coloring and runtime machinery. Not a product mode.
static cl::opt<bool>
    ClForceAllUnsafe("safe-stack-force-all-unsafe",
                     cl::desc("move every alloca to the unsafe stack"),
                     cl::Hidden, cl::init(false));

namespace {

/// The SafeStack pass splits the stack of each function into the safe
/// stack, which is only accessed through memory safe dereferences (as
/// determined statically), and the unsafe stack, which contains all
/// local variables that are accessed in ways that we can't prove to
/// be safe.
class SafeStack {
  Function &F;
  const TargetLoweringBase &TL;
  const LibcallLoweringInfo &Libcalls;
  const DataLayout &DL;
  DomTreeUpdater *DTU;
  ScalarEvolution &SE;

  Type *StackPtrTy;
  Type *AddrTy;
  Type *Int32Ty;

  Value *UnsafeStackPtr = nullptr;

  /// Every llvm.va_start in the function, and the size/alignment of the
  /// varargs register save area the target wants relocated onto the unsafe
  /// stack. Set only when the target opts in via getVarArgsSaveAreaInfo.
  SmallVector<IntrinsicInst *, 2> VAStarts;
  std::optional<TargetLoweringBase::VarArgsSaveAreaInfo> VarArgsSaveArea;

  /// Address of the reserved save area, once the frame is laid out.
  Value *VarArgsSlot = nullptr;

  /// Whether variadic calls in this function stage their stack arguments on
  /// the unsafe stack, and whether the function actually contains one. A
  /// musttail variadic call is tracked apart because it is not bumped.
  bool PositionConvention = false;
  bool HasVariadicCall = false;
  bool HasMustTailVariadicCall = false;

  /// When set, classification uses the strict SP-relative predicate
  /// instead of the classic escape/bounds analysis.
  std::optional<TargetLoweringBase::SPRelativePolicy> SPPolicy;

  /// Running total of objects kept on the native stack, against
  /// SPPolicy->MaxNativeFrameSize.
  uint64_t NativeFrameSize = 0;

  /// Unsafe stack alignment. Each stack frame must ensure that the stack is
  /// aligned to this value. We need to re-align the unsafe stack if the
  /// alignment of any object on the stack exceeds this value.
  ///
  /// 16 seems like a reasonable upper bound on the alignment of objects that we
  /// might expect to appear on the stack on most common targets.
  static constexpr Align StackAlignment = Align::Constant<16>();

  /// Return the value of the stack canary.
  Value *getStackGuard(IRBuilder<> &IRB, Function &F);

  /// Load stack guard from the frame and check if it has changed.
  void checkStackGuard(IRBuilder<> &IRB, Function &F, Instruction &RI,
                       AllocaInst *StackGuardSlot, Value *StackGuard);

  /// Find all static allocas, dynamic allocas, return instructions and
  /// stack restore points (exception unwind blocks and setjmp calls) in the
  /// given function and append them to the respective vectors.
  void findInsts(Function &F, SmallVectorImpl<AllocaInst *> &StaticAllocas,
                 SmallVectorImpl<AllocaInst *> &DynamicAllocas,
                 SmallVectorImpl<Argument *> &ByValArguments,
                 SmallVectorImpl<Instruction *> &Returns,
                 SmallVectorImpl<Instruction *> &StackRestorePoints);

  /// Calculate the allocation size of a given alloca. Returns 0 if the
  /// size can not be statically determined.
  uint64_t getStaticAllocaAllocationSize(const AllocaInst* AI);

  /// Allocate space for all static allocas in \p StaticAllocas,
  /// replace allocas with pointers into the unsafe stack.
  ///
  /// \returns A pointer to the top of the unsafe stack after all unsafe static
  /// allocas are allocated.
  Value *moveStaticAllocasToUnsafeStack(IRBuilder<> &IRB, Function &F,
                                        ArrayRef<AllocaInst *> StaticAllocas,
                                        ArrayRef<Argument *> ByValArguments,
                                        Instruction *BasePointer,
                                        AllocaInst *StackGuardSlot);

  /// Generate code to restore the stack after all stack restore points
  /// in \p StackRestorePoints.
  ///
  /// \returns A local variable in which to maintain the dynamic top of the
  /// unsafe stack if needed.
  AllocaInst *
  createStackRestorePoints(IRBuilder<> &IRB, Function &F,
                           ArrayRef<Instruction *> StackRestorePoints,
                           Value *StaticTop, bool NeedDynamicTop);

  /// Replace all allocas in \p DynamicAllocas with code to allocate
  /// space dynamically on the unsafe stack and store the dynamic unsafe stack
  /// top to \p DynamicTop if non-null.
  void moveDynamicAllocasToUnsafeStack(Function &F, Value *UnsafeStackPtr,
                                       AllocaInst *DynamicTop,
                                       ArrayRef<AllocaInst *> DynamicAllocas);

  bool IsSafeStackAlloca(const Value *AllocaPtr, uint64_t AllocaSize);

  /// Constant-geometry test: every access to \p AllocaPtr folds into a
  /// [SP + constant] memory operand, so its address is never materialized
  /// into a general-purpose register.
  bool IsSPRelativeAlloca(const Value *AllocaPtr);

  /// Decide whether \p AllocaPtr (an alloca or a byval argument) may stay on
  /// the native stack, under whichever classification policy is in effect.
  bool keepOnNativeStack(const Value *AllocaPtr, uint64_t AllocaSize);

  bool IsMemIntrinsicSafe(const MemIntrinsic *MI, const Use &U,
                          const Value *AllocaPtr, uint64_t AllocaSize);
  bool IsAccessSafe(Value *Addr, TypeSize Size, const Value *AllocaPtr,
                    uint64_t AllocaSize);
  bool IsAccessSafe(Value *Addr, uint64_t Size, const Value *AllocaPtr,
                    uint64_t AllocaSize);

  bool ShouldInlinePointerAddress(CallInst &CI);
  void TryInlinePointerAddress();

public:
  SafeStack(Function &F, const TargetLoweringBase &TL,
            const LibcallLoweringInfo &Libcalls, const DataLayout &DL,
            DomTreeUpdater *DTU, ScalarEvolution &SE)
      : F(F), TL(TL), Libcalls(Libcalls), DL(DL), DTU(DTU), SE(SE),
        StackPtrTy(DL.getAllocaPtrType(F.getContext())),
        AddrTy(DL.getAddressType(StackPtrTy)),
        Int32Ty(Type::getInt32Ty(F.getContext())) {}

  // Run the transformation on the associated function.
  // Returns whether the function was changed.
  bool run();
};

uint64_t SafeStack::getStaticAllocaAllocationSize(const AllocaInst* AI) {
  if (auto Size = AI->getAllocationSize(DL))
    if (Size->isFixed())
      return Size->getFixedValue();
  return 0;
}

bool SafeStack::IsAccessSafe(Value *Addr, TypeSize AccessSize,
                             const Value *AllocaPtr, uint64_t AllocaSize) {
  if (AccessSize.isScalable()) {
    // In case we don't know the size at compile time we cannot verify if the
    // access is safe.
    return false;
  }
  return IsAccessSafe(Addr, AccessSize.getFixedValue(), AllocaPtr, AllocaSize);
}

bool SafeStack::IsAccessSafe(Value *Addr, uint64_t AccessSize,
                             const Value *AllocaPtr, uint64_t AllocaSize) {
  const SCEV *AddrExpr = SE.getSCEV(Addr);
  const auto *Base = dyn_cast<SCEVUnknown>(SE.getPointerBase(AddrExpr));
  if (!Base || Base->getValue() != AllocaPtr) {
    LLVM_DEBUG(
        dbgs() << "[SafeStack] "
               << (isa<AllocaInst>(AllocaPtr) ? "Alloca " : "ByValArgument ")
               << *AllocaPtr << "\n"
               << "SCEV " << *AddrExpr << " not directly based on alloca\n");
    return false;
  }

  const SCEV *Expr = SE.removePointerBase(AddrExpr);
  uint64_t BitWidth = SE.getTypeSizeInBits(Expr->getType());
  ConstantRange AccessStartRange = SE.getUnsignedRange(Expr);
  ConstantRange SizeRange =
      ConstantRange(APInt(BitWidth, 0), APInt(BitWidth, AccessSize));
  ConstantRange AccessRange = AccessStartRange.add(SizeRange);
  ConstantRange AllocaRange =
      ConstantRange(APInt(BitWidth, 0), APInt(BitWidth, AllocaSize));
  bool Safe = AllocaRange.contains(AccessRange);

  LLVM_DEBUG(
      dbgs() << "[SafeStack] "
             << (isa<AllocaInst>(AllocaPtr) ? "Alloca " : "ByValArgument ")
             << *AllocaPtr << "\n"
             << "            Access " << *Addr << "\n"
             << "            SCEV " << *Expr
             << " U: " << SE.getUnsignedRange(Expr)
             << ", S: " << SE.getSignedRange(Expr) << "\n"
             << "            Range " << AccessRange << "\n"
             << "            AllocaRange " << AllocaRange << "\n"
             << "            " << (Safe ? "safe" : "unsafe") << "\n");

  return Safe;
}

bool SafeStack::IsMemIntrinsicSafe(const MemIntrinsic *MI, const Use &U,
                                   const Value *AllocaPtr,
                                   uint64_t AllocaSize) {
  if (auto MTI = dyn_cast<MemTransferInst>(MI)) {
    if (MTI->getRawSource() != U && MTI->getRawDest() != U)
      return true;
  } else {
    if (MI->getRawDest() != U)
      return true;
  }

  auto Len = MI->getLengthInBytes();
  // Non-constant size => unsafe. FIXME: try SCEV getRange.
  if (!Len) return false;
  return IsAccessSafe(U, Len->getZExtValue(), AllocaPtr, AllocaSize);
}

/// Check whether a given allocation must be put on the safe
/// stack or not. The function analyzes all uses of AI and checks whether it is
/// only accessed in a memory safe way (as decided statically).
bool SafeStack::IsSafeStackAlloca(const Value *AllocaPtr, uint64_t AllocaSize) {
  // Go through all uses of this alloca and check whether all accesses to the
  // allocated object are statically known to be memory safe and, hence, the
  // object can be placed on the safe stack.
  SmallPtrSet<const Value *, 16> Visited;
  SmallVector<const Value *, 8> WorkList;
  WorkList.push_back(AllocaPtr);

  // A DFS search through all uses of the alloca in bitcasts/PHI/GEPs/etc.
  while (!WorkList.empty()) {
    const Value *V = WorkList.pop_back_val();
    for (const Use &UI : V->uses()) {
      auto I = cast<const Instruction>(UI.getUser());
      assert(V == UI.get());

      switch (I->getOpcode()) {
      case Instruction::Load:
        if (!IsAccessSafe(UI, DL.getTypeStoreSize(I->getType()), AllocaPtr,
                          AllocaSize))
          return false;
        break;

      case Instruction::VAArg:
        // "va-arg" from a pointer is safe.
        break;
      case Instruction::Store:
        if (V == I->getOperand(0)) {
          // Stored the pointer - conservatively assume it may be unsafe.
          LLVM_DEBUG(dbgs()
                     << "[SafeStack] Unsafe alloca: " << *AllocaPtr
                     << "\n            store of address: " << *I << "\n");
          return false;
        }

        if (!IsAccessSafe(UI, DL.getTypeStoreSize(I->getOperand(0)->getType()),
                          AllocaPtr, AllocaSize))
          return false;
        break;

      case Instruction::Ret:
        // Information leak.
        return false;

      case Instruction::Call:
      case Instruction::Invoke: {
        const CallBase &CS = *cast<CallBase>(I);

        if (I->isLifetimeStartOrEnd())
          continue;

        if (const MemIntrinsic *MI = dyn_cast<MemIntrinsic>(I)) {
          if (!IsMemIntrinsicSafe(MI, UI, AllocaPtr, AllocaSize)) {
            LLVM_DEBUG(dbgs()
                       << "[SafeStack] Unsafe alloca: " << *AllocaPtr
                       << "\n            unsafe memintrinsic: " << *I << "\n");
            return false;
          }
          continue;
        }

        // LLVM 'nocapture' attribute is only set for arguments whose address
        // is not stored, passed around, or used in any other non-trivial way.
        // We assume that passing a pointer to an object as a 'nocapture
        // readnone' argument is safe.
        // FIXME: a more precise solution would require an interprocedural
        // analysis here, which would look at all uses of an argument inside
        // the function being called.
        auto B = CS.arg_begin(), E = CS.arg_end();
        for (const auto *A = B; A != E; ++A)
          if (A->get() == V)
            if (!(CS.doesNotCapture(A - B) && (CS.doesNotAccessMemory(A - B) ||
                                               CS.doesNotAccessMemory()))) {
              LLVM_DEBUG(dbgs() << "[SafeStack] Unsafe alloca: " << *AllocaPtr
                                << "\n            unsafe call: " << *I << "\n");
              return false;
            }
        continue;
      }

      default:
        if (Visited.insert(I).second)
          WorkList.push_back(cast<const Instruction>(I));
      }
    }
  }

  // All uses of the alloca are safe, we can place it on the safe stack.
  return true;
}

bool SafeStack::IsSPRelativeAlloca(const Value *AllocaPtr) {
  SmallPtrSet<const Value *, 16> Visited;
  SmallVector<const Value *, 8> WorkList;
  WorkList.push_back(AllocaPtr);

  while (!WorkList.empty()) {
    const Value *V = WorkList.pop_back_val();

    // Block locality. Instruction selection works one block at a time, so a
    // derived pointer used outside its defining block is exported through a
    // virtual register -- which is exactly the address materialization this
    // predicate exists to prevent. The alloca itself is exempt: a static
    // alloca is a FrameIndex, a constant that gets rematerialized per block
    // rather than exported.
    if (V != AllocaPtr) {
      const auto *Derived = cast<Instruction>(V);
      for (const User *U : V->users())
        if (cast<Instruction>(U)->getParent() != Derived->getParent()) {
          LLVM_DEBUG(dbgs() << "[SafeStack] Not SP-relative: "
                            << *AllocaPtr << "\n            cross-block use: "
                            << *Derived << "\n");
          return false;
        }
    }

    for (const Use &UI : V->uses()) {
      const auto *I = cast<Instruction>(UI.getUser());

      switch (I->getOpcode()) {
      case Instruction::Load:
        // A load's only pointer-typed operand is the one being dereferenced.
        break;

      case Instruction::Store:
        // Storing the pointer itself hands the address to memory.
        if (V == I->getOperand(0)) {
          LLVM_DEBUG(dbgs() << "[SafeStack] Not SP-relative: "
                            << *AllocaPtr << "\n            store of address: "
                            << *I << "\n");
          return false;
        }
        break;

      case Instruction::GetElementPtr:
        // A variable index lowers to base-plus-index addressing, which needs
        // the base in a register. Note this is stricter than the classic
        // analysis, which would accept an index SCEV proves to be in bounds.
        if (!cast<GetElementPtrInst>(I)->hasAllConstantIndices()) {
          LLVM_DEBUG(dbgs() << "[SafeStack] Not SP-relative: "
                            << *AllocaPtr << "\n            variable index: "
                            << *I << "\n");
          return false;
        }
        [[fallthrough]];

      case Instruction::BitCast:
      case Instruction::AddrSpaceCast:
        if (Visited.insert(I).second)
          WorkList.push_back(I);
        break;

      case Instruction::Call:
      case Instruction::Invoke:
        // Lifetime markers and debug intrinsics do not form an address.
        // Everything else -- including memcpy and friends, whose libcall
        // takes the pointer in a register -- does.
        if (I->isLifetimeStartOrEnd() || isa<DbgInfoIntrinsic>(I))
          break;
        LLVM_DEBUG(dbgs() << "[SafeStack] Not SP-relative: " << *AllocaPtr
                          << "\n            call: " << *I << "\n");
        return false;

      default:
        // phi and select of derived pointers, ptrtoint, icmp, ret, va_arg:
        // all either leak the address or require it in a register.
        LLVM_DEBUG(dbgs() << "[SafeStack] Not SP-relative: " << *AllocaPtr
                          << "\n            unsupported use: " << *I << "\n");
        return false;
      }
    }
  }

  return true;
}

bool SafeStack::keepOnNativeStack(const Value *AllocaPtr, uint64_t AllocaSize) {
  if (ClForceAllUnsafe)
    return false;

  if (!SPPolicy)
    return IsSafeStackAlloca(AllocaPtr, AllocaSize);

  if (const auto *AI = dyn_cast<AllocaInst>(AllocaPtr)) {
    // Stated explicitly rather than inherited from the use walk: a VLA with
    // no uses passes the walk vacuously, yet still emits a runtime-sized
    // stack adjustment.
    if (!AI->isStaticAlloca())
      return false;
    // Over-aligned objects force stack realignment, which makes the backend
    // address fixed objects off the frame pointer instead of the stack
    // pointer.
    if (AI->getAlign() > StackAlignment)
      return false;
  } else if (MaybeAlign A = cast<Argument>(AllocaPtr)->getParamAlign()) {
    if (*A > StackAlignment)
      return false;
  }

  if (!IsSPRelativeAlloca(AllocaPtr))
    return false;

  // Budget clause: keep the native frame within the displacement range that
  // the target can address, evicting oversized objects so that valid code
  // keeps compiling rather than tripping a later hard error.
  if (AllocaSize > SPPolicy->MaxNativeFrameSize - NativeFrameSize)
    return false;
  NativeFrameSize += AllocaSize;
  return true;
}

Value *SafeStack::getStackGuard(IRBuilder<> &IRB, Function &F) {
  Value *StackGuardVar = TL.getIRStackGuard(IRB, Libcalls);
  Module *M = F.getParent();

  if (!StackGuardVar) {
    TL.insertSSPDeclarations(*M, Libcalls);
    return IRB.CreateIntrinsic(Intrinsic::stackguard, {});
  }

  return IRB.CreateLoad(StackPtrTy, StackGuardVar, "StackGuard");
}

void SafeStack::findInsts(Function &F,
                          SmallVectorImpl<AllocaInst *> &StaticAllocas,
                          SmallVectorImpl<AllocaInst *> &DynamicAllocas,
                          SmallVectorImpl<Argument *> &ByValArguments,
                          SmallVectorImpl<Instruction *> &Returns,
                          SmallVectorImpl<Instruction *> &StackRestorePoints) {
  for (Instruction &I : instructions(&F)) {
    // Under the position convention the backend bumps the unsafe stack
    // pointer around each variadic call. An exception unwinding past such a
    // call would leave the bump in place, so this function needs restore
    // points even if it has nothing of its own on the unsafe stack --
    // otherwise a catch-and-retry loop leaks the unsafe stack without bound.
    // A musttail call is never bumped (the callee is meant to see the area
    // its own caller staged), so it needs nothing restored.
    if (PositionConvention)
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (CB->getFunctionType()->isVarArg()) {
          auto *CI = dyn_cast<CallInst>(CB);
          if (CI && CI->isMustTailCall())
            HasMustTailVariadicCall = true;
          else
            HasVariadicCall = true;
        }

    if (auto AI = dyn_cast<AllocaInst>(&I)) {
      ++NumAllocas;

      uint64_t Size = getStaticAllocaAllocationSize(AI);
      if (keepOnNativeStack(AI, Size))
        continue;

      if (AI->isStaticAlloca()) {
        ++NumUnsafeStaticAllocas;
        StaticAllocas.push_back(AI);
      } else {
        ++NumUnsafeDynamicAllocas;
        DynamicAllocas.push_back(AI);
      }
    } else if (auto RI = dyn_cast<ReturnInst>(&I)) {
      if (CallInst *CI = I.getParent()->getTerminatingMustTailCall())
        Returns.push_back(CI);
      else
        Returns.push_back(RI);
    } else if (auto CI = dyn_cast<CallInst>(&I)) {
      // setjmps require stack restore.
      if (CI->getCalledFunction() && CI->canReturnTwice())
        StackRestorePoints.push_back(CI);
      // Key off the intrinsic, not the va_list alloca: llvm.va_start may
      // target heap or global memory, in which case there is no alloca.
      if (auto *II = dyn_cast<IntrinsicInst>(CI))
        if (II->getIntrinsicID() == Intrinsic::vastart)
          VAStarts.push_back(II);
    } else if (auto LP = dyn_cast<LandingPadInst>(&I)) {
      // Exception landing pads require stack restore.
      StackRestorePoints.push_back(LP);
    } else if (auto II = dyn_cast<IntrinsicInst>(&I)) {
      if (II->getIntrinsicID() == Intrinsic::gcroot)
        report_fatal_error(
            "gcroot intrinsic not compatible with safestack attribute");
    }
  }
  for (Argument &Arg : F.args()) {
    if (!Arg.hasByValAttr())
      continue;
    uint64_t Size = DL.getTypeStoreSize(Arg.getParamByValType());
    // A byval that passes stays where the caller put it, with no copy at all;
    // only failing ones get an unsafe slot and the memcpy that fills it.
    if (keepOnNativeStack(&Arg, Size))
      continue;

    ++NumUnsafeByValArguments;
    ByValArguments.push_back(&Arg);
  }
}

AllocaInst *
SafeStack::createStackRestorePoints(IRBuilder<> &IRB, Function &F,
                                    ArrayRef<Instruction *> StackRestorePoints,
                                    Value *StaticTop, bool NeedDynamicTop) {
  assert(StaticTop && "The stack top isn't set.");

  if (StackRestorePoints.empty())
    return nullptr;

  // We need the current value of the shadow stack pointer to restore
  // after longjmp or exception catching.

  // FIXME: On some platforms this could be handled by the longjmp/exception
  // runtime itself.

  AllocaInst *DynamicTop = nullptr;
  if (NeedDynamicTop) {
    // If we also have dynamic alloca's, the stack pointer value changes
    // throughout the function. For now we store it in an alloca.
    DynamicTop = IRB.CreateAlloca(StackPtrTy, /*ArraySize=*/nullptr,
                                  "unsafe_stack_dynamic_ptr");
    IRB.CreateStore(StaticTop, DynamicTop);
  }

  // Restore current stack pointer after longjmp/exception catch.
  for (Instruction *I : StackRestorePoints) {
    ++NumUnsafeStackRestorePoints;

    IRB.SetInsertPoint(I->getNextNode());
    Value *CurrentTop =
        DynamicTop ? IRB.CreateLoad(StackPtrTy, DynamicTop) : StaticTop;
    IRB.CreateStore(CurrentTop, UnsafeStackPtr);
  }

  return DynamicTop;
}

void SafeStack::checkStackGuard(IRBuilder<> &IRB, Function &F, Instruction &RI,
                                AllocaInst *StackGuardSlot, Value *StackGuard) {
  Value *V = IRB.CreateLoad(StackPtrTy, StackGuardSlot);
  Value *Cmp = IRB.CreateICmpNE(StackGuard, V);

  auto SuccessProb = BranchProbabilityInfo::getBranchProbStackProtector(true);
  auto FailureProb = BranchProbabilityInfo::getBranchProbStackProtector(false);
  MDNode *Weights = MDBuilder(F.getContext())
                        .createBranchWeights(SuccessProb.getNumerator(),
                                             FailureProb.getNumerator());
  Instruction *CheckTerm =
      SplitBlockAndInsertIfThen(Cmp, &RI, /* Unreachable */ true, Weights, DTU);
  IRBuilder<> IRBFail(CheckTerm);
  // FIXME: respect -fsanitize-trap / -ftrap-function here?
  RTLIB::LibcallImpl StackChkFailImpl =
      Libcalls.getLibcallImpl(RTLIB::STACKPROTECTOR_CHECK_FAIL);
  if (StackChkFailImpl == RTLIB::Unsupported) {
    F.getContext().emitError(
        "no libcall available for stackprotector check fail");
    return;
  }

  StringRef StackChkFailName =
      RTLIB::RuntimeLibcallsInfo::getLibcallImplName(StackChkFailImpl);

  FunctionCallee StackChkFail =
      F.getParent()->getOrInsertFunction(StackChkFailName, IRB.getVoidTy());
  IRBFail.CreateCall(StackChkFail, {});
}

/// We explicitly compute and set the unsafe stack layout for all unsafe
/// static alloca instructions. We save the unsafe "base pointer" in the
/// prologue into a local variable and restore it in the epilogue.
Value *SafeStack::moveStaticAllocasToUnsafeStack(
    IRBuilder<> &IRB, Function &F, ArrayRef<AllocaInst *> StaticAllocas,
    ArrayRef<Argument *> ByValArguments, Instruction *BasePointer,
    AllocaInst *StackGuardSlot) {
  if (StaticAllocas.empty() && ByValArguments.empty() && !VarArgsSaveArea)
    return BasePointer;

  DIBuilder DIB(*F.getParent());

  StackLifetime SSC(F, StaticAllocas, StackLifetime::LivenessType::May);
  static const StackLifetime::LiveRange NoColoringRange(1, true);
  if (ClColoring)
    SSC.run();

  for (const auto *I : SSC.getMarkers()) {
    auto *Op = dyn_cast<Instruction>(I->getOperand(1));
    const_cast<IntrinsicInst *>(I)->eraseFromParent();
    // Remove the operand bitcast, too, if it has no more uses left.
    if (Op && Op->use_empty())
      Op->eraseFromParent();
  }

  // Unsafe stack always grows down.
  StackLayout SSL(StackAlignment);
  if (StackGuardSlot) {
    SSL.addObject(StackGuardSlot, getStaticAllocaAllocationSize(StackGuardSlot),
                  StackGuardSlot->getAlign(), SSC.getFullLiveRange());
  }

  // The varargs register save area is an ordinary static frame object, so it
  // sits above StaticTop and the restore points, dynamic allocas and stack
  // coloring all keep working unchanged. The layout handle is opaque, so the
  // function itself serves as a key that can never collide with an alloca.
  if (VarArgsSaveArea)
    SSL.addObject(&F, VarArgsSaveArea->Size, VarArgsSaveArea->Alignment,
                  SSC.getFullLiveRange());

  for (Argument *Arg : ByValArguments) {
    Type *Ty = Arg->getParamByValType();
    uint64_t Size = DL.getTypeStoreSize(Ty);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    // Ensure the object is properly aligned.
    Align Align = DL.getPrefTypeAlign(Ty);
    if (auto A = Arg->getParamAlign())
      Align = std::max(Align, *A);
    SSL.addObject(Arg, Size, Align, SSC.getFullLiveRange());
  }

  for (AllocaInst *AI : StaticAllocas) {
    uint64_t Size = getStaticAllocaAllocationSize(AI);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    SSL.addObject(AI, Size, AI->getAlign(),
                  ClColoring ? SSC.getLiveRange(AI) : NoColoringRange);
  }

  SSL.computeLayout();
  Align FrameAlignment = SSL.getFrameAlignment();

  // FIXME: tell SSL that we start at a less-then-MaxAlignment aligned location
  // (AlignmentSkew).
  if (FrameAlignment > StackAlignment) {
    // Re-align the base pointer according to the max requested alignment.
    IRB.SetInsertPoint(BasePointer->getNextNode());
    BasePointer = IRB.CreateIntrinsic(
        StackPtrTy, Intrinsic::ptrmask,
        {BasePointer, ConstantInt::get(AddrTy, ~(FrameAlignment.value() - 1))});
  }

  IRB.SetInsertPoint(BasePointer->getNextNode());

  if (VarArgsSaveArea) {
    unsigned Offset = SSL.getObjectOffset(&F);
    VarArgsSlot = IRB.CreatePtrAdd(BasePointer,
                                   ConstantInt::get(Int32Ty, -Offset),
                                   "varargs_reg_save_area");
  }

  if (StackGuardSlot) {
    unsigned Offset = SSL.getObjectOffset(StackGuardSlot);
    Value *Off =
        IRB.CreatePtrAdd(BasePointer, ConstantInt::get(Int32Ty, -Offset));
    Value *NewAI =
        IRB.CreateBitCast(Off, StackGuardSlot->getType(), "StackGuardSlot");

    // Replace alloc with the new location.
    StackGuardSlot->replaceAllUsesWith(NewAI);
    StackGuardSlot->eraseFromParent();
  }

  for (Argument *Arg : ByValArguments) {
    unsigned Offset = SSL.getObjectOffset(Arg);
    MaybeAlign Align(SSL.getObjectAlignment(Arg));
    Type *Ty = Arg->getParamByValType();

    uint64_t Size = DL.getTypeStoreSize(Ty);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    Value *Off =
        IRB.CreatePtrAdd(BasePointer, ConstantInt::get(Int32Ty, -Offset));
    Value *NewArg = IRB.CreateBitCast(Off, Arg->getType(),
                                      Arg->getName() + ".unsafe-byval");

    // Replace alloc with the new location.
    replaceDbgDeclare(Arg, BasePointer, DIB, DIExpression::ApplyOffset,
                      -Offset);
    Arg->replaceAllUsesWith(NewArg);
    IRB.SetInsertPoint(cast<Instruction>(NewArg)->getNextNode());
    IRB.CreateMemCpy(Off, Align, Arg, Arg->getParamAlign(), Size);
  }

  // Allocate space for every unsafe static AllocaInst on the unsafe stack.
  for (AllocaInst *AI : StaticAllocas) {
    IRB.SetInsertPoint(AI);
    unsigned Offset = SSL.getObjectOffset(AI);

    replaceDbgDeclare(AI, BasePointer, DIB, DIExpression::ApplyOffset, -Offset);
    replaceDbgValueForAlloca(AI, BasePointer, DIB, -Offset);

    // Replace uses of the alloca with the new location.
    // Insert address calculation close to each use to work around PR27844.
    std::string Name = std::string(AI->getName()) + ".unsafe";
    while (!AI->use_empty()) {
      Use &U = *AI->use_begin();
      Instruction *User = cast<Instruction>(U.getUser());

      // Drop lifetime markers now that this is no longer an alloca.
      // SafeStack has already performed its own stack coloring.
      if (User->isLifetimeStartOrEnd()) {
        User->eraseFromParent();
        continue;
      }

      Instruction *InsertBefore;
      if (auto *PHI = dyn_cast<PHINode>(User))
        InsertBefore = PHI->getIncomingBlock(U)->getTerminator();
      else
        InsertBefore = User;

      IRBuilder<> IRBUser(InsertBefore);
      Value *Off =
          IRBUser.CreatePtrAdd(BasePointer, ConstantInt::get(Int32Ty, -Offset));
      Value *Replacement =
          IRBUser.CreateAddrSpaceCast(Off, AI->getType(), Name);

      if (auto *PHI = dyn_cast<PHINode>(User))
        // PHI nodes may have multiple incoming edges from the same BB (why??),
        // all must be updated at once with the same incoming value.
        PHI->setIncomingValueForBlock(PHI->getIncomingBlock(U), Replacement);
      else
        U.set(Replacement);
    }

    AI->eraseFromParent();
  }

  // Re-align BasePointer so that our callees would see it aligned as
  // expected.
  // FIXME: no need to update BasePointer in leaf functions.
  unsigned FrameSize = alignTo(SSL.getFrameSize(), StackAlignment);

  MDBuilder MDB(F.getContext());
  SmallVector<Metadata *, 2> Data;
  Data.push_back(MDB.createString("unsafe-stack-size"));
  Data.push_back(MDB.createConstant(ConstantInt::get(Int32Ty, FrameSize)));
  MDNode *MD = MDTuple::get(F.getContext(), Data);
  F.setMetadata(LLVMContext::MD_annotation, MD);

  // Update shadow stack pointer in the function epilogue.
  IRB.SetInsertPoint(BasePointer->getNextNode());

  Value *StaticTop =
      IRB.CreatePtrAdd(BasePointer, ConstantInt::get(Int32Ty, -FrameSize),
                       "unsafe_stack_static_top");
  IRB.CreateStore(StaticTop, UnsafeStackPtr);
  return StaticTop;
}

void SafeStack::moveDynamicAllocasToUnsafeStack(
    Function &F, Value *UnsafeStackPtr, AllocaInst *DynamicTop,
    ArrayRef<AllocaInst *> DynamicAllocas) {
  DIBuilder DIB(*F.getParent());

  for (AllocaInst *AI : DynamicAllocas) {
    IRBuilder<> IRB(AI);

    // Compute the new SP value (after AI).
    Value *Size = IRB.CreateAllocationSize(AddrTy, AI);
    Value *SP = IRB.CreateLoad(StackPtrTy, UnsafeStackPtr);
    SP = IRB.CreatePtrAdd(SP, IRB.CreateNeg(Size));

    // Align the SP value to satisfy the AllocaInst and stack alignments.
    auto Align = std::max(AI->getAlign(), StackAlignment);

    Value *NewTop = IRB.CreateIntrinsic(
        StackPtrTy, Intrinsic::ptrmask,
        {SP, ConstantInt::getSigned(AddrTy, ~uint64_t(Align.value() - 1))});

    // Save the stack pointer.
    IRB.CreateStore(NewTop, UnsafeStackPtr);
    if (DynamicTop)
      IRB.CreateStore(NewTop, DynamicTop);

    Value *NewAI = IRB.CreatePointerCast(NewTop, AI->getType());
    if (AI->hasName() && isa<Instruction>(NewAI))
      NewAI->takeName(AI);

    replaceDbgDeclare(AI, NewAI, DIB, DIExpression::ApplyOffset, 0);
    AI->replaceAllUsesWith(NewAI);
    AI->eraseFromParent();
  }

  if (!DynamicAllocas.empty()) {
    // Now go through the instructions again, replacing stacksave/stackrestore.
    for (Instruction &I : llvm::make_early_inc_range(instructions(&F))) {
      auto *II = dyn_cast<IntrinsicInst>(&I);
      if (!II)
        continue;

      if (II->getIntrinsicID() == Intrinsic::stacksave) {
        IRBuilder<> IRB(II);
        Instruction *LI = IRB.CreateLoad(StackPtrTy, UnsafeStackPtr);
        LI->takeName(II);
        II->replaceAllUsesWith(LI);
        II->eraseFromParent();
      } else if (II->getIntrinsicID() == Intrinsic::stackrestore) {
        IRBuilder<> IRB(II);
        Instruction *SI = IRB.CreateStore(II->getArgOperand(0), UnsafeStackPtr);
        SI->takeName(II);
        assert(II->use_empty());
        II->eraseFromParent();
      }
    }
  }
}

bool SafeStack::ShouldInlinePointerAddress(CallInst &CI) {
  Function *Callee = CI.getCalledFunction();
  if (CI.hasFnAttr(Attribute::AlwaysInline) &&
      isInlineViable(*Callee).isSuccess())
    return true;
  if (Callee->isInterposable() || Callee->hasFnAttribute(Attribute::NoInline) ||
      CI.isNoInline())
    return false;
  return true;
}

void SafeStack::TryInlinePointerAddress() {
  auto *CI = dyn_cast<CallInst>(UnsafeStackPtr);
  if (!CI)
    return;

  if(F.hasOptNone())
    return;

  Function *Callee = CI->getCalledFunction();
  if (!Callee || Callee->isDeclaration())
    return;

  if (!ShouldInlinePointerAddress(*CI))
    return;

  InlineFunctionInfo IFI;
  InlineFunction(*CI, IFI);
}

bool SafeStack::run() {
  assert(F.hasFnAttribute(Attribute::SafeStack) &&
         "Can't run SafeStack on a function without the attribute");
  assert(!F.isDeclaration() && "Can't run SafeStack on a function declaration");

  ++NumFunctions;

  SmallVector<AllocaInst *, 16> StaticAllocas;
  SmallVector<AllocaInst *, 4> DynamicAllocas;
  SmallVector<Argument *, 4> ByValArguments;
  SmallVector<Instruction *, 4> Returns;

  // Collect all points where stack gets unwound and needs to be restored
  // This is only necessary because the runtime (setjmp and unwind code) is
  // not aware of the unsafe stack and won't unwind/restore it properly.
  // To work around this problem without changing the runtime, we insert
  // instrumentation to restore the unsafe stack pointer when necessary.
  SmallVector<Instruction *, 4> StackRestorePoints;

  PositionConvention = TL.useSafeStackVarArgPositionConvention(F);

  SPPolicy = TL.getSPRelativePolicy(F);

  // Find all static and dynamic alloca instructions that must be moved to the
  // unsafe stack, all return instructions and stack restore points.
  findInsts(F, StaticAllocas, DynamicAllocas, ByValArguments, Returns,
            StackRestorePoints);

  // If the target can host the varargs register save area on the unsafe
  // stack, reserving it is what makes the frame exist: a function whose only
  // unsafe object is the save area (a heap or global va_list, say) still gets
  // the full prologue/epilogue and restore-point machinery via the normal
  // path, with no special case anywhere.
  if (!VAStarts.empty())
    VarArgsSaveArea = TL.getVarArgsSaveAreaInfo(F);

  bool HasUnsafeFrame = !StaticAllocas.empty() || !DynamicAllocas.empty() ||
                        !ByValArguments.empty() || VarArgsSaveArea;

  // A frameless variadic musttail thunk forwards the area its own caller
  // staged, because nothing moves the unsafe stack pointer between the
  // thunk's entry and the forwarded call. Give the thunk a frame and that
  // stops being true -- the entry prologue stores a lowered stack pointer,
  // so the callee's entry-time value no longer names the area's base -- and
  // a musttail call cannot be un-tail-called to repair it.
  if (HasMustTailVariadicCall && HasUnsafeFrame)
    report_fatal_error("musttail variadic call in '" + F.getName() +
                       "', which needs an unsafe stack frame, is incompatible "
                       "with the SafeStack varargs position convention");

  if (StaticAllocas.empty() && DynamicAllocas.empty() &&
      ByValArguments.empty() && StackRestorePoints.empty() && !VarArgsSaveArea &&
      !HasVariadicCall)
    return false; // Nothing to do in this function.

  if (HasUnsafeFrame)
    ++NumUnsafeStackFunctions; // This function has the unsafe stack.

  if (!StackRestorePoints.empty())
    ++NumUnsafeStackRestorePointsFunctions;

  IRBuilder<> IRB(&F.front(), F.begin()->getFirstInsertionPt());
  // Calls must always have a debug location, or else inlining breaks. So
  // we explicitly set a artificial debug location here.
  if (DISubprogram *SP = F.getSubprogram())
    IRB.SetCurrentDebugLocation(
        DILocation::get(SP->getContext(), SP->getScopeLine(), 0, SP));
  if (SafeStackUsePointerAddress) {
    // FIXME: A more correct implementation of SafeStackUsePointerAddress would
    // change the libcall availability in RuntimeLibcallsInfo
    StringRef SafestackPointerAddressName =
        RTLIB::RuntimeLibcallsInfo::getLibcallImplName(
            RTLIB::impl___safestack_pointer_address);

    FunctionCallee Fn = F.getParent()->getOrInsertFunction(
        SafestackPointerAddressName, IRB.getPtrTy(0));
    UnsafeStackPtr = IRB.CreateCall(Fn);
  } else {
    UnsafeStackPtr = TL.getSafeStackPointerLocation(IRB, Libcalls);
    if (!UnsafeStackPtr) {
      F.getContext().emitError(
          "no location available for safestack pointer address");
      UnsafeStackPtr = PoisonValue::get(StackPtrTy);
    }
  }

  // Load the current stack pointer (we'll also use it as a base pointer).
  // FIXME: use a dedicated register for it ?
  Instruction *BasePointer =
      IRB.CreateLoad(StackPtrTy, UnsafeStackPtr, false, "unsafe_stack_ptr");
  assert(BasePointer->getType() == StackPtrTy);

  AllocaInst *StackGuardSlot = nullptr;
  // FIXME: implement weaker forms of stack protector.
  if (F.hasFnAttribute(Attribute::StackProtect) ||
      F.hasFnAttribute(Attribute::StackProtectStrong) ||
      F.hasFnAttribute(Attribute::StackProtectReq)) {
    Value *StackGuard = getStackGuard(IRB, F);
    StackGuardSlot = IRB.CreateAlloca(StackPtrTy, nullptr);
    IRB.CreateStore(StackGuard, StackGuardSlot);

    for (Instruction *RI : Returns) {
      IRBuilder<> IRBRet(RI);
      checkStackGuard(IRBRet, F, *RI, StackGuardSlot, StackGuard);
    }
  }

  // The top of the unsafe stack after all unsafe static allocas are
  // allocated.
  Value *StaticTop = moveStaticAllocasToUnsafeStack(
      IRB, F, StaticAllocas, ByValArguments, BasePointer, StackGuardSlot);

  // Hand the reserved save area to instruction selection. save.regs must stay
  // in the entry block, where the argument registers it spills are still
  // live; the va_start variants may sit anywhere.
  if (VarArgsSlot) {
    assert(isa<Instruction>(VarArgsSlot) &&
           "save area address should be computed in the entry block");
    IRB.SetInsertPoint(cast<Instruction>(VarArgsSlot)->getNextNode());
    IRB.CreateIntrinsic(Intrinsic::safestack_vararg_save_regs, {},
                        {VarArgsSlot});

    for (IntrinsicInst *VAStart : VAStarts) {
      Value *ArgList = VAStart->getArgOperand(0);
      IRBuilder<> IRBVA(VAStart);
      IRBVA.CreateIntrinsic(Intrinsic::vastart_safestack, {ArgList->getType()},
                            {ArgList, VarArgsSlot, BasePointer});
      VAStart->eraseFromParent();
    }
  }

  // Safe stack object that stores the current unsafe stack top. It is updated
  // as unsafe dynamic (non-constant-sized) allocas are allocated and freed.
  // This is only needed if we need to restore stack pointer after longjmp
  // or exceptions, and we have dynamic allocations.
  // FIXME: a better alternative might be to store the unsafe stack pointer
  // before setjmp / invoke instructions.
  AllocaInst *DynamicTop = createStackRestorePoints(
      IRB, F, StackRestorePoints, StaticTop, !DynamicAllocas.empty());

  // Handle dynamic allocas.
  moveDynamicAllocasToUnsafeStack(F, UnsafeStackPtr, DynamicTop,
                                  DynamicAllocas);

  // Restore the unsafe stack pointer before each return.
  for (Instruction *RI : Returns) {
    IRB.SetInsertPoint(RI);
    IRB.CreateStore(BasePointer, UnsafeStackPtr);
  }

  TryInlinePointerAddress();

  LLVM_DEBUG(dbgs() << "[SafeStack]     safestack applied\n");
  return true;
}

class SafeStackLegacyPass : public FunctionPass {
  const TargetMachine *TM = nullptr;

public:
  static char ID; // Pass identification, replacement for typeid..

  SafeStackLegacyPass() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LibcallLoweringInfoWrapper>();
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addPreserved<DominatorTreeWrapperPass>();
  }

  bool runOnFunction(Function &F) override {
    LLVM_DEBUG(dbgs() << "[SafeStack] Function: " << F.getName() << "\n");

    if (!F.hasFnAttribute(Attribute::SafeStack)) {
      LLVM_DEBUG(dbgs() << "[SafeStack]     safestack is not requested"
                           " for this function\n");
      return false;
    }

    if (F.isDeclaration()) {
      LLVM_DEBUG(dbgs() << "[SafeStack]     function definition"
                           " is not available\n");
      return false;
    }

    TM = &getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
    const TargetSubtargetInfo *Subtarget = TM->getSubtargetImpl(F);
    auto *TL = Subtarget->getTargetLowering();
    if (!TL)
      report_fatal_error("TargetLowering instance is required");

    const LibcallLoweringInfo &Libcalls =
        getAnalysis<LibcallLoweringInfoWrapper>().getLibcallLowering(
            *F.getParent(), *Subtarget);

    auto *DL = &F.getDataLayout();
    auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
    auto &ACT = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

    // Compute DT and LI only for functions that have the attribute.
    // This is only useful because the legacy pass manager doesn't let us
    // compute analyzes lazily.

    DominatorTree *DT;
    bool ShouldPreserveDominatorTree;
    std::optional<DominatorTree> LazilyComputedDomTree;

    // Do we already have a DominatorTree available from the previous pass?
    // Note that we should *NOT* require it, to avoid the case where we end up
    // not needing it, but the legacy PM would have computed it for us anyways.
    if (auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>()) {
      DT = &DTWP->getDomTree();
      ShouldPreserveDominatorTree = true;
    } else {
      // Otherwise, we need to compute it.
      LazilyComputedDomTree.emplace(F);
      DT = &*LazilyComputedDomTree;
      ShouldPreserveDominatorTree = false;
    }

    // Likewise, lazily compute loop info.
    LoopInfo LI(*DT);

    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);

    ScalarEvolution SE(F, TLI, ACT, *DT, LI);

    return SafeStack(F, *TL, Libcalls, *DL,
                     ShouldPreserveDominatorTree ? &DTU : nullptr, SE)
        .run();
  }
};

} // end anonymous namespace

PreservedAnalyses SafeStackPass::run(Function &F,
                                     FunctionAnalysisManager &FAM) {
  LLVM_DEBUG(dbgs() << "[SafeStack] Function: " << F.getName() << "\n");

  if (!F.hasFnAttribute(Attribute::SafeStack)) {
    LLVM_DEBUG(dbgs() << "[SafeStack]     safestack is not requested"
                         " for this function\n");
    return PreservedAnalyses::all();
  }

  if (F.isDeclaration()) {
    LLVM_DEBUG(dbgs() << "[SafeStack]     function definition"
                         " is not available\n");
    return PreservedAnalyses::all();
  }

  const TargetSubtargetInfo *Subtarget = TM->getSubtargetImpl(F);
  auto *TL = Subtarget->getTargetLowering();

  auto &DL = F.getDataLayout();

  // preserve DominatorTree
  auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

  auto &MAMProxy = FAM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  const LibcallLoweringModuleAnalysisResult *LibcallLowering =
      MAMProxy.getCachedResult<LibcallLoweringModuleAnalysis>(*F.getParent());

  if (!LibcallLowering) {
    F.getContext().emitError("'" + LibcallLoweringModuleAnalysis::name() +
                             "' analysis required");
    return PreservedAnalyses::all();
  }

  const LibcallLoweringInfo &Libcalls =
      LibcallLowering->getLibcallLowering(*Subtarget);

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);

  bool Changed = SafeStack(F, *TL, Libcalls, DL, &DTU, SE).run();

  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

char SafeStackLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(SafeStackLegacyPass, DEBUG_TYPE,
                      "Safe Stack instrumentation pass", false, false)
INITIALIZE_PASS_DEPENDENCY(LibcallLoweringInfoWrapper)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(SafeStackLegacyPass, DEBUG_TYPE,
                    "Safe Stack instrumentation pass", false, false)

FunctionPass *llvm::createSafeStackPass() { return new SafeStackLegacyPass(); }
