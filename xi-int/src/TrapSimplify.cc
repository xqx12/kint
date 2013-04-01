#define DEBUG_TYPE "trap-simplify"
#include "Trap.h"
#include <llvm/Instructions.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpander.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Support/InstIterator.h>
#include <iostream>

using std::cerr;

STATISTIC(NumConstRemoved,    "Number of constant trap calls removed");
STATISTIC(NumPtrSubRewritten, "Number of pointer subtraction rewritten");
STATISTIC(NumTrapPtrSubRewritten, "Number of pointer subtraction traps rewritten");

namespace {

// Simplify arithmetic operations, e.g., constant, pointer subtraction. 
struct TrapSimplify : llvm::FunctionPass {
	static char ID;
	TrapSimplify() : llvm::FunctionPass(ID) {
		llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
		llvm::initializeScalarEvolutionPass(Registry);
	}

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
		AU.addRequired<llvm::ScalarEvolution>();
	}

	virtual bool runOnFunction(llvm::Function &F) {
		cerr << "TrapSimplify::runOnFunction called\n" ;
		bool Changed = false;
		SE = 0;
		for (llvm::inst_iterator i = inst_begin(F),
		     e = inst_end(F); i != e; ) {
			llvm::Instruction *I = &*i;
			++i;
			if (llvm::isa<llvm::SubOperator>(I)) {
				Changed |= simplifyPtrSub(I);
				continue;
			}
			llvm::StringRef Name = Trap::getName(I);
			if (Name.empty())
				continue;
			if (stripConstTrap(I)) {
				Changed = true;
				continue;
			}
			if (Name.startswith("sdiv") || Name.startswith("srem"))
				Changed |= stripSDivRemTrap(I);
			else if (Name.startswith("ssub") || Name.startswith("usub"))
				Changed |= simplifyPtrSub(I);
		}
		return Changed;
	}

private:
	llvm::ScalarEvolution *SE;
	bool stripConstTrap(llvm::Instruction *);
	bool stripSDivRemTrap(llvm::Instruction *);
	bool simplifyPtrSub(llvm::Instruction *);
};

} // anonymous namespace

// After constant propagation some traps are with constant parameters,
// which are unnecessary to check.
bool TrapSimplify::stripConstTrap(llvm::Instruction *I) {
	llvm::CallSite CS(I);
	llvm::CallSite::arg_iterator i = CS.arg_begin(), e = CS.arg_end();
	for (; i != e; ++i) {
		llvm::Value *V = *i;
		if (!llvm::isa<llvm::ConstantInt>(V))
			return false;
	}
	assert(I->use_empty());
	Trap::RecursivelyDeleteTriviallyDeadInstructions(I);
	++NumConstRemoved;
	return true;
}

// Only need to keep x / 0 or x / -1.
bool TrapSimplify::stripSDivRemTrap(llvm::Instruction *I) {
	llvm::ConstantInt *Divisor = llvm::dyn_cast<llvm::ConstantInt>(I->getOperand(1));
	if (!Divisor || Divisor->isZero() || Divisor->isAllOnesValue())
		return false;
	Trap::RecursivelyDeleteTriviallyDeadInstructions(I);
	++NumConstRemoved;
	return true;
}

// This tries to simplify pointer subtractions, e.g.,
//    p0 = base + x
//    p1 = base + y
//     n = p1 - p0
//    -------------
// =>  n = y - x
// See CVE-2011-1593 (kernel/pid.c) for an example.
bool TrapSimplify::simplifyPtrSub(llvm::Instruction *I) {
	llvm::Value *LV = I->getOperand(0), *RV = I->getOperand(1);
	llvm::PtrToIntInst *LHSInst, *RHSInst;
	LHSInst = llvm::dyn_cast<llvm::PtrToIntInst>(LV);
	RHSInst = llvm::dyn_cast<llvm::PtrToIntInst>(RV);
	if (!LHSInst || !RHSInst)
		return false;
	if (!SE)
		SE = &getAnalysis<llvm::ScalarEvolution>();
	const llvm::SCEV *LHS, *RHS;
	LHS = SE->getSCEV(LHSInst->getOperand(0));
	RHS = SE->getSCEV(RHSInst->getOperand(0));
	const llvm::SCEV *S = SE->getAddExpr(
		LHS, SE->getNegativeSCEV(RHS), llvm::SCEV::FlagNSW
	);
	llvm::SCEVExpander Rewriter(*SE, "");
	llvm::Value *V = Rewriter.expandCodeFor(S, LV->getType(), I);
	if (llvm::isa<llvm::CallInst>(I)) {
		// trap.[su]sub.*
		if (llvm::Instruction *NewInstr = llvm::dyn_cast<llvm::Instruction>(V)) {
			NewInstr->setDebugLoc(I->getDebugLoc());
			generateTrap(NewInstr);
		}
		assert(I->use_empty());
		++NumTrapPtrSubRewritten;
	}
	else {
		// sub instruction
		I->replaceAllUsesWith(V);
		++NumPtrSubRewritten;
	}
	Trap::RecursivelyDeleteTriviallyDeadInstructions(I);
	return true;
}

char TrapSimplify::ID;

static llvm::RegisterPass<TrapSimplify>
X("trap-simplify", "Simplify trap calls");

llvm::Pass *createTrapSimplifyPass() {
	return new TrapSimplify;
}
