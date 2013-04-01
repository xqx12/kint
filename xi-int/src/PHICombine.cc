#define DEBUG_TYPE "phicombine"
#include <llvm/Instructions.h>
#include <llvm/Pass.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <iostream>

using std::cerr;

namespace {

struct PHICombine : llvm::FunctionPass {
	static char ID;
	PHICombine() : FunctionPass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
	}

	virtual bool runOnFunction(llvm::Function &F) {
		cerr << "PHICombine::runOnFunction called\n" ;
		releaseMemory();
		bool Changed = false;
		llvm::inst_iterator i = inst_begin(F), e = inst_end(F);
		while (i != e) {
			llvm::Instruction *I = &*i++;
			if (Visited.count(I))
				continue;
			if (llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(I))
				Changed |= rewrite(BO);
			else if (llvm::CastInst *CI = llvm::dyn_cast<llvm::CastInst>(I))
				Changed |= rewrite(CI);
		}
		return Changed;
	}

	virtual void releaseMemory() {
		Visited.clear();
	}

private:
	llvm::SmallPtrSet<llvm::Instruction *, 16> Visited;

	void replaceInstWithPHINode(llvm::Instruction *I, llvm::PHINode *PN) {
		I->replaceAllUsesWith(PN);
		if (I->hasName() && !PN->hasName())
			PN->takeName(I);
		I->eraseFromParent();
	}

	bool rewrite(llvm::CastInst *I) {
		cerr << "PHICombine::rewrite(llvm::CastInst *I) called\n" ;
		llvm::PHINode *RHS = llvm::dyn_cast<llvm::PHINode>(I->getOperand(0));
		if (!RHS)
			return false;
		unsigned n = RHS->getNumIncomingValues();
		llvm::PHINode *PN = llvm::PHINode::Create(I->getDestTy(), n, "", RHS);
		for (unsigned i = 0; i != n; ++i) {
			llvm::BasicBlock *Pred = RHS->getIncomingBlock(i);
			llvm::CastInst *CI = llvm::cast<llvm::CastInst>(I->clone());
			CI->setOperand(0, RHS->getIncomingValue(i));
			CI->insertBefore(Pred->getTerminator());
			PN->addIncoming(CI, Pred);
			Visited.insert(CI);
		}
		replaceInstWithPHINode(I, PN);
		return true;
	}

	bool rewrite(llvm::BinaryOperator *I) {
		llvm::Value *LHS = I->getOperand(0), *RHS = I->getOperand(1);
		llvm::PHINode *LPN, *RPN;
		LPN = llvm::dyn_cast<llvm::PHINode>(LHS);
		RPN = llvm::dyn_cast<llvm::PHINode>(RHS);
		llvm::PHINode *PN;
		if (LPN && RPN) {
			if (LPN->getParent() == RPN->getParent())
				PN = rewriteLR(I, LPN, RPN);
			else
				return false;
		}
		else if (LPN && llvm::isa<llvm::Constant>(RHS))
			PN = rewriteL(I, LPN, RHS);
		else if (llvm::isa<llvm::Constant>(LHS) && RPN)
			PN = rewriteR(I, LHS, RPN);
		else
			return false;
		replaceInstWithPHINode(I, PN);
		return true;
	}

	llvm::PHINode *rewriteL(llvm::BinaryOperator *I, llvm::PHINode *LHS, llvm::Value *RHS) {
		unsigned n = LHS->getNumIncomingValues();
		llvm::PHINode *PN = llvm::PHINode::Create(I->getType(), n, "", LHS);
		for (unsigned i = 0; i != n; ++i) {
			llvm::BasicBlock *Pred = LHS->getIncomingBlock(i);
			llvm::BinaryOperator *BO = llvm::cast<llvm::BinaryOperator>(I->clone());
			BO->setOperand(0, LHS->getIncomingValue(i));
			BO->setOperand(1, RHS);
			BO->insertBefore(Pred->getTerminator());
			PN->addIncoming(BO, Pred);
			Visited.insert(BO);
		}
		return PN;
	}

	llvm::PHINode *rewriteR(llvm::BinaryOperator *I, llvm::Value *LHS, llvm::PHINode *RHS) {
		unsigned n = RHS->getNumIncomingValues();
		llvm::PHINode *PN = llvm::PHINode::Create(I->getType(), n, "", RHS);
		for (unsigned i = 0; i != n; ++i) {
			llvm::BasicBlock *Pred = RHS->getIncomingBlock(i);
			llvm::BinaryOperator *BO = llvm::cast<llvm::BinaryOperator>(I->clone());
			BO->setOperand(0, LHS);
			BO->setOperand(1, RHS->getIncomingValue(i));
			BO->insertBefore(Pred->getTerminator());
			PN->addIncoming(BO, Pred);
			Visited.insert(BO);
		}
		return PN;
	}

	llvm::PHINode *rewriteLR(llvm::BinaryOperator *I, llvm::PHINode *LHS, llvm::PHINode *RHS) {
		unsigned n = LHS->getNumIncomingValues();
		assert(n == RHS->getNumIncomingValues());
		llvm::PHINode *PN = llvm::PHINode::Create(LHS->getType(), n, "", LHS);
		for (unsigned i = 0; i != n; ++i) {
			llvm::BasicBlock *Pred = LHS->getIncomingBlock(i);
			assert(Pred == RHS->getIncomingBlock(i));
			llvm::BinaryOperator *BO = llvm::cast<llvm::BinaryOperator>(I->clone());
			BO->setOperand(0, LHS->getIncomingValue(i));
			BO->setOperand(1, RHS->getIncomingValue(i));
			BO->insertBefore(Pred->getTerminator());
			PN->addIncoming(BO, Pred);
			Visited.insert(BO);
		}
		llvm::BasicBlock *BB = LHS->getParent();
		assert(BB == RHS->getParent());
		return PN;
	}
};

} // namespace

char PHICombine::ID;

static llvm::RegisterPass<PHICombine>
X("phicombine", "Combine instructions using constant PHIs");
