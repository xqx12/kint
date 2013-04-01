#define DEBUG_TYPE "trap-combine"
#include "Trap.h"
#include <llvm/Instructions.h>
#include <llvm/Pass.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Analysis/Dominators.h>
#include <llvm/Support/InstIterator.h>
#include <iostream>


using std::cerr;

namespace {

struct TrapCombine : llvm::FunctionPass {
	static char ID;
	TrapCombine() : llvm::FunctionPass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
		AU.addRequired<llvm::DominatorTree>();
	}

	virtual bool runOnFunction(llvm::Function &F);
};

} // anonymous namespace

bool TrapCombine::runOnFunction(llvm::Function &F) {
	cerr << "TrapCombine::runOnFunction"  <<  "\n";
	typedef std::pair<
		llvm::Function *,                       // trap function
		std::pair<llvm::Value *, llvm::Value *> // at most two args 
	> TrapCall;
	typedef llvm::SmallPtrSet<llvm::CallInst *, 8> CallSiteSet;
	typedef llvm::DenseMap<
		TrapCall,                               // call argument
		CallSiteSet                             // call instruction
	> TrapCallMap;
	TrapCallMap TCM;
	llvm::DominatorTree &DT = getAnalysis<llvm::DominatorTree>();
	bool Changed = false;
	for (llvm::inst_iterator i = inst_begin(F),
	     e = inst_end(F); i != e; ) {
		llvm::CallInst *I = llvm::dyn_cast<llvm::CallInst>(&*i);
		++i;
		if (!I)
			continue;
		llvm::StringRef Name = Trap::getName(I);
		if (Name.empty())
			continue;
		assert(I->getNumArgOperands() <= 2);
		llvm::Value *LHS = I->getArgOperand(0), *RHS = 0;
		if (I->getNumArgOperands() == 2) {
			RHS = I->getArgOperand(1);
			// Normalize commutative operands.
			if (Trap::isCommutative(Name))
			    if (LHS < RHS)
				std::swap(LHS, RHS);
		}
		CallSiteSet &CSS = TCM[std::make_pair(
			I->getCalledFunction(), std::make_pair(LHS, RHS)
		)];
		// Decide whether to keep I.
		bool dominated = false;
		for (CallSiteSet::iterator i = CSS.begin(), e = CSS.end(); i != e; ++i) {
			llvm::CallInst *CI = *i;
			if (DT.dominates(CI, I)) {
				dominated = true;
				break;
			}
		}
		if (dominated) {
			I->eraseFromParent();
			Changed = true;
		}
		else
			CSS.insert(I);
	}
	return Changed;
}

char TrapCombine::ID;

static llvm::RegisterPass<TrapCombine>
X("trap-combine", "Combine trap calls");

llvm::Pass *createTrapCombinePass() {
	return new TrapCombine;
}
