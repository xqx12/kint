#define DEBUG_TYPE "trap-aa"
#include "Trap.h"
#include <llvm/Metadata.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/CaptureTracking.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <iostream>

using std::cerr;

namespace {

struct TrapAliasAnalysis : llvm::FunctionPass, llvm::AliasAnalysis {
	static char ID;
	TrapAliasAnalysis() : llvm::FunctionPass(ID) {
		llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
		llvm::initializeAliasAnalysisAnalysisGroup(Registry);
		llvm::initializeScalarEvolutionPass(Registry);
	}

	// FunctionPass

	virtual void *getAdjustedAnalysisPointer(llvm::AnalysisID ID) {
		if (ID == &AliasAnalysis::ID)
			return (AliasAnalysis*)this;
		return this;
	}

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesAll();
		AU.addRequired<llvm::ScalarEvolution>();
		AliasAnalysis::getAnalysisUsage(AU);
	}

	virtual bool doInitialization(llvm::Module &);

	virtual bool runOnFunction(llvm::Function &) {
		InitializeAliasAnalysis(this);
		SE = &getAnalysis<llvm::ScalarEvolution>();
		return false;
	}

	// AliasAnalysis

	virtual AliasResult alias(const Location &LocA, const Location &LocB);

	virtual ModRefResult getModRefInfo(llvm::ImmutableCallSite, const Location &);

	virtual ModRefResult
	getModRefInfo(llvm::ImmutableCallSite CS1, llvm::ImmutableCallSite CS2) {
		if (safeToIgnore(CS1) || safeToIgnore(CS2))
			return NoModRef;
		return AliasAnalysis::getModRefInfo(CS1, CS2);
	}

private:
	typedef llvm::DenseMap<const llvm::Argument *, llvm::StringSet<> > ArgAnnoMap;

	llvm::ScalarEvolution *SE;
	ArgAnnoMap Arg2Anno;

	bool hasArgAnno(llvm::ImmutableCallSite CS, unsigned Idx, llvm::StringRef Str) const;

	bool safeToIgnore(llvm::ImmutableCallSite CS);

	bool mayCompatible(llvm::Type *T0, llvm::Type *T1);

	llvm::Value *getBaseValue(const llvm::SCEV *S);
};

} // anonymous namespace

bool TrapAliasAnalysis::doInitialization(llvm::Module &M) {
	cerr << "TrapAliasAnalysis::doInitialization called\n" ;
	// Load argument annotations.
	llvm::NamedMDNode *NMD = M.getNamedMetadata(TRAP_ARG_ANNOTATIONS);
	if (!NMD)
		goto out;
	for (unsigned i = 0, n = NMD->getNumOperands(); i != n; ++i) {
		llvm::MDNode *MD = NMD->getOperand(i);
		if (!MD->getOperand(0))
			continue;
		assert(MD->getNumOperands() == 3);
                if (!MD->getOperand(0))
                        continue;
		llvm::Function *F = llvm::cast<llvm::Function>(MD->getOperand(0));
		llvm::Function::arg_iterator AI = F->arg_begin();
		for (uint64_t Idx = llvm::cast<llvm::ConstantInt>(MD->getOperand(1))->getZExtValue(); Idx; --Idx)
			++AI;
		llvm::StringRef Anno = llvm::cast<llvm::MDString>(MD->getOperand(2))->getString();
		Arg2Anno[AI].insert(Anno);
	}
out:
	return false;
}

static bool isStackAndIntPointers(llvm::Value *V0, llvm::Value *V1) {
	return llvm::isa<llvm::AllocaInst>(V0) && llvm::isa<llvm::IntToPtrInst>(V1);
}

llvm::AliasAnalysis::AliasResult
TrapAliasAnalysis::alias(const Location &LocA, const Location &LocB) {
	if (LocA.Size == 0 || LocB.Size == 0)
		return NoAlias;

	llvm::Value *AP = const_cast<llvm::Value *>(LocA.Ptr);
	llvm::Value *BP = const_cast<llvm::Value *>(LocB.Ptr);
	if (AP == BP)
		return MustAlias;
	// Assume arguments do not alias with others.
	if (llvm::isa<llvm::Argument>(AP) || llvm::isa<llvm::Argument>(BP))
		return NoAlias;

	// Dirty hack: uverbs-2010-4649.
	if (isStackAndIntPointers(AP, BP) || isStackAndIntPointers(BP, AP))
		return NoAlias;

	const llvm::SCEV *AS = SE->getSCEV(AP);
	const llvm::SCEV *BS = SE->getSCEV(BP);
	if (AS == BS)
		return MustAlias;

	llvm::Value *AO = getBaseValue(AS);
	llvm::Value *BO = getBaseValue(BS);
	if (AO && BO && !(AO == AP && BO == BP)) {
		if (!mayCompatible(AO->getType(), BO->getType()))
			return NoAlias;
		if (isNoAlias(Location(AO), Location(BO)))
			return NoAlias;
	}

	return AliasAnalysis::alias(LocA, LocB);
}

llvm::AliasAnalysis::ModRefResult
TrapAliasAnalysis::getModRefInfo(llvm::ImmutableCallSite CS, const Location &Loc) {
	if (safeToIgnore(CS))
		return NoModRef;
	const llvm::SCEV *S = SE->getSCEV(const_cast<llvm::Value *>(Loc.Ptr));
	llvm::Value *Base = getBaseValue(S);
	// If the location is derived from a function argument or stack,
	// and it is not captured nor passed into the call, we assume that
	// the call will not modify it.
	if (Base
	    && (llvm::isa<llvm::AllocaInst>(Base)
	    || (llvm::isa<llvm::Argument>(Base)
	        && !llvm::PointerMayBeCaptured(Base, false, true)))) {
		ModRefResult MRR = NoModRef;
		llvm::ImmutableCallSite::arg_iterator i, e;
		unsigned k = 0;
		for (i = CS.arg_begin(), e = CS.arg_end(); i != e; ++i, ++k) {
			llvm::Value *V = *i;
			if (!V->getType()->isPointerTy())
				continue;
			if (hasArgAnno(CS, k, "in")) {
				MRR = Ref;
				continue;
			}
			if (!isNoAlias(Location(V), Location(Base))) {
				MRR = ModRef;
				break;
			}
		}
		if (!(MRR & Mod))
			return MRR;
	}
	return AliasAnalysis::getModRefInfo(CS, Loc);
}

bool TrapAliasAnalysis::hasArgAnno(llvm::ImmutableCallSite CS, unsigned Idx, llvm::StringRef Str) const {
	if (const llvm::Function *F = CS.getCalledFunction()) {
		llvm::Function::const_arg_iterator AI = F->arg_begin();
		for (; Idx; --Idx)
			++AI;
		ArgAnnoMap::const_iterator i = Arg2Anno.find(AI);
		if (i != Arg2Anno.end())
			return i->second.count(Str);
	}
	return false;
}

bool TrapAliasAnalysis::safeToIgnore(llvm::ImmutableCallSite CS) {
	if (CS.onlyReadsMemory())
		return true;
	// Assume it's ok to bypass noreturn functions.
	if (CS.doesNotReturn())
		return true;
	if (Trap::isa(CS.getInstruction()))
		return true;
	if (const llvm::Function *F = CS.getCalledFunction()) {
		llvm::StringRef Name = F->getName();
		if (Name.startswith("llvm.stack") || Name.startswith("llvm.lifetime"))
			return true;
	}
	return false;
}

bool TrapAliasAnalysis::mayCompatible(llvm::Type *T0, llvm::Type *T1) {
	llvm::StructType *ST0 = llvm::dyn_cast<llvm::StructType>(
		llvm::cast<llvm::PointerType>(T0)->getElementType()
	);
	llvm::StructType *ST1 = llvm::dyn_cast<llvm::StructType>(
		llvm::cast<llvm::PointerType>(T1)->getElementType()
	);
	if (!ST0 || !ST1)
		return true;
	return ST0->isLayoutIdentical(ST1);
}

// From ScalarEvolutionAliasAnalysis
llvm::Value *TrapAliasAnalysis::getBaseValue(const llvm::SCEV *S) {
	if (const llvm::SCEVAddRecExpr *ARE = llvm::dyn_cast<llvm::SCEVAddRecExpr>(S)) {
		return getBaseValue(ARE->getStart());
	} else if (const llvm::SCEVAddExpr *AE = llvm::dyn_cast<llvm::SCEVAddExpr>(S)) {
		// AE's type is that of the last element
		// which is likely to be the pointer.
		if (AE->getType()->isPointerTy())
			return getBaseValue(*(AE->op_end() - 1));
	} else if (const llvm::SCEVUnknown *U = llvm::dyn_cast<llvm::SCEVUnknown>(S)) {
		return U->getValue();
	}
	return 0;
}

char TrapAliasAnalysis::ID;
char &TrapAliasAnalysisID = TrapAliasAnalysis::ID;

static llvm::RegisterPass<TrapAliasAnalysis>
X("trap-aa", "Recognize alias idioms", false, true);

static llvm::RegisterAnalysisGroup<llvm::AliasAnalysis>
Y(X);

llvm::Pass *createTrapAliasAnalysisPass() {
	return new TrapAliasAnalysis;
}
