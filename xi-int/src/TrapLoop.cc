#define DEBUG_TYPE "trap-loop"
#include "Trap.h"
#include <llvm/Pass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpander.h>
#include <llvm/Support/InstIterator.h>
#include <iostream>


using std::cerr;


namespace {

struct TrapLoop : llvm::FunctionPass {
	static char ID;
	TrapLoop() : llvm::FunctionPass(ID) {
		llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
		llvm::initializeLoopInfoPass(Registry);
		llvm::initializeScalarEvolutionPass(Registry);
	}

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
		AU.addRequired<llvm::LoopInfo>();
		AU.addRequired<llvm::ScalarEvolution>();
	}

	virtual bool runOnFunction(llvm::Function &);

private:
	llvm::LoopInfo *LI;
	llvm::ScalarEvolution *SE;

	bool expand(llvm::CallInst *, llvm::Loop *, llvm::BasicBlock *BB);
};

} // anonymous namespace

bool TrapLoop::runOnFunction(llvm::Function &F) {
	cerr << "TrapLoop::runOnFunction"  <<  "\n";
	bool Changed = false;
	LI = &getAnalysis<llvm::LoopInfo>();
	SE = &getAnalysis<llvm::ScalarEvolution>();
	for (llvm::Function::iterator bi = F.begin(), be = F.end(); bi != be; ++bi) {
		llvm::BasicBlock *BB = bi;
		llvm::Loop *L = LI->getLoopFor(BB);
		if (!L)
			continue;
		llvm::BasicBlock *PredBB = L->getLoopPredecessor();
		if (!PredBB)
			continue;
		if (!SE->hasLoopInvariantBackedgeTakenCount(L))
			continue;
		for (llvm::BasicBlock::iterator i = BB->begin(), e = BB->end(); i != e; ++i) {
			if (llvm::CallInst *I = llvm::dyn_cast<llvm::CallInst>(i))
				Changed |= expand(I, L, PredBB);
		}
	}
	return Changed;
}

bool TrapLoop::expand(llvm::CallInst *I, llvm::Loop *L, llvm::BasicBlock *BB) {
	llvm::StringRef Name = Trap::getName(I);
	if (Name.empty())
		return false;
	// Move a[N] only if N is constant.
	if (Name.startswith("index") && Name[5] == TRAP_SEPARATOR[0])
		return false;
	// Compute the exit value for each operand.
	unsigned n = I->getNumArgOperands();
	std::vector<const llvm::SCEV *> SCEVArgs(n);
	L = L->getParentLoop();
	for (unsigned i = 0; i != n; ++i) {
		llvm::Value *V = I->getArgOperand(i);
		const llvm::SCEV *S = SE->getSCEV(V);
		const llvm::SCEV *ExitVal = SE->getSCEVAtScope(S, L);
		if (ExitVal == S)
			return false;
		if (!SE->dominates(ExitVal, BB))
			return false;
		SCEVArgs[i] = ExitVal;
	}

	// Rewrite the trap outside of the loop.
	llvm::SCEVExpander Rewriter(*SE, "");
	llvm::Instruction *IP = BB->getTerminator();
	llvm::Type *T = I->getArgOperand(0)->getType();
	std::vector<llvm::Value *> Args(n);
	for (unsigned i = 0; i != n; ++i)
		Args[i] = Rewriter.expandCodeFor(SCEVArgs[i], T, IP);
	llvm::CallInst *NewInstr = llvm::CallInst::Create(I->getCalledValue(), Args, I->getName(), IP);
	NewInstr->setDebugLoc(I->getDebugLoc());

	return true;
}

char TrapLoop::ID;

static llvm::RegisterPass<TrapLoop>
X("trap-loop", "Move trap calls out of loops");

llvm::Pass *createTrapLoopPass() {
	return new TrapLoop;
}
