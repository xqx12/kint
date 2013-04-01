#define DEBUG_TYPE "spconstprop"
#include <llvm/Pass.h>
#include <llvm/Instructions.h>
#include <llvm/Function.h>
#include <llvm/Support/raw_ostream.h>

namespace {
struct SPConstProp : llvm::FunctionPass {
	static char ID;
	SPConstProp() : FunctionPass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
	}

	virtual bool runOnFunction(llvm::Function &F) {
		bool Changed = false;
		for (llvm::Function::iterator bit = F.begin(); bit != F.end(); bit++) {
			llvm::BasicBlock *BB = &*bit;
			llvm::BasicBlock *PredBB = BB->getSinglePredecessor();
			if (!PredBB) continue;
			llvm::SwitchInst *SI = llvm::dyn_cast<llvm::SwitchInst>(PredBB->getTerminator());
			if (!SI) continue;
			llvm::Instruction *SwitchV = llvm::dyn_cast<llvm::Instruction>(SI->getCondition());
			if (!SwitchV) continue;
			llvm::ConstantInt *CI = SI->findCaseDest(BB);
			if (!CI) continue;
			for (llvm::BasicBlock::iterator iit = BB->begin(); iit != BB->end(); iit++) {
				llvm::Instruction *I = &*iit;
				for (size_t i = 0; i < I->getNumOperands(); i++) {
					llvm::Value *Op = I->getOperand(i);
					if (llvm::isa<llvm::PHINode>(Op)) {
						llvm::PHINode *PI = llvm::dyn_cast<llvm::PHINode>(Op);
						llvm::SwitchInst *SI2 = getSwitchFromPHI(PI);
						if (!SI2) continue;
						if (SI2->getCondition() != SI->getCondition()) continue;
						if (SI2->getParent() != SwitchV->getParent()) continue;
						unsigned CaseIndex = SI2->findCaseValue(CI);
						llvm::BasicBlock *CaseBB;
						if (CaseIndex == 0)
							CaseBB = SI2->getParent();
						else
							CaseBB = SI2->getSuccessor(CaseIndex);
						llvm::Value *CaseV = PI->getIncomingValueForBlock(CaseBB);
						assert(CaseV);
						I->setOperand(i, CaseV);
						Changed = true;
					}
				}
			}
		}
		return Changed;
	}

private:
	llvm::SwitchInst* getSwitchFromPHI(llvm::PHINode *PI) {
		llvm::BasicBlock *SwitchBlock = NULL;
		for (unsigned i = 0; i < PI->getNumIncomingValues(); i++) {
			llvm::BasicBlock *BB = PI->getIncomingBlock(i);
			if (llvm::isa<llvm::SwitchInst>(BB->getTerminator())) {
				if (!SwitchBlock)
					SwitchBlock = BB;
				else
					return NULL;
			}
		}
		if (SwitchBlock->getTerminator()->getNumSuccessors() != PI->getNumIncomingValues())
			return NULL;
		for (unsigned i = 0; i < PI->getNumIncomingValues(); i++) {
			llvm::BasicBlock *BB = PI->getIncomingBlock(i);
			if (BB != SwitchBlock) {
				llvm::TerminatorInst *TI = BB->getTerminator();
				if (TI->getNumSuccessors() != 1) return NULL;
				llvm::BasicBlock *PredBB = BB->getSinglePredecessor();
				if (PredBB != SwitchBlock) return NULL;
			}
		}
		return llvm::dyn_cast<llvm::SwitchInst>(SwitchBlock->getTerminator());
	}
};

} // anonymous namespace

char SPConstProp::ID;

static llvm::RegisterPass<SPConstProp>
X("spcp", "Constant Propagation for PHI instructions generated after PHICombine and GEPUnroll");
