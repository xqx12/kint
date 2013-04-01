#define DEBUG_TYPE "gepunroll"
#include <llvm/Constants.h>
#include <llvm/GlobalVariable.h>
#include <llvm/Instructions.h>
#include <llvm/Pass.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/GetElementPtrTypeIterator.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

namespace {

/// This pass unrolls pattern
///   GEP GV, 0, x
///   load GV
/// for each possible value of x, given GV as a constant.
struct GEPUnroll : llvm::FunctionPass {
	typedef llvm::DenseMap<llvm::Use *, llvm::ConstantRange> IndexRangeMap;
	typedef llvm::SmallPtrSet<llvm::GetElementPtrInst *, 16> GEPSet;
	typedef llvm::SmallPtrSet<llvm::PHINode *, 16> PHISet;
	typedef llvm::SmallPtrSet<llvm::Value *, 16>   ValueSet;

	static char ID;
	GEPUnroll() : llvm::FunctionPass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.addRequired<llvm::ScalarEvolution>();
		AU.addPreserved<llvm::ScalarEvolution>();
	}

	virtual bool runOnFunction(llvm::Function &F) {
		llvm::errs() << "GEPUnroll::runOnFunction called\n" ;
		releaseMemory();
		GEPSet GEPs;
		for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
			llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&*i);
			if (!LI)
				continue;
			llvm::Value *V = LI->getPointerOperand();
			llvm::GetElementPtrInst *GEP = llvm::dyn_cast<
				llvm::GetElementPtrInst>(V);
			if (GEP) {
				llvm::GlobalVariable *GV = llvm::dyn_cast<
					llvm::GlobalVariable>(GEP->getOperand(0));
				if (GV && GV->isConstant()) {
					if (GEPs.insert(GEP))
						fillIndexRange(LI, GEP);
				}
			}
		}
		GEPs.clear();
		if (IndexRanges.empty())
			return false;	
		while (!IndexRanges.empty()) {
			const IndexRangeMap::value_type &Pair = *IndexRanges.begin();
			llvm::Use *U = Pair.first;
			llvm::GetElementPtrInst *GEP = llvm::cast<
				llvm::GetElementPtrInst>(U->getUser());
			for (llvm::Value::use_iterator i = GEP->use_begin(),
			     e = GEP->use_end(); i != e; ++i) {
				if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(*i))
					rewrite(U, Pair.second, GEP, LI);
			}
			// The rest non-const indices (if any) have been cloned
			// and added to IndexRanges, so remove them here.
			for (llvm::Instruction::op_iterator i = GEP->idx_begin(),
			     e = GEP->idx_end(); i != e; ++i)
				IndexRanges.erase(i);
		}
		return true;
	}

	virtual void releaseMemory() {
		IndexRanges.clear();
	}

private:
	IndexRangeMap IndexRanges;

	void fillIndexRange(llvm::LoadInst *I, llvm::GetElementPtrInst *GEP) {
		llvm::User::op_iterator op = GEP->idx_begin();
		for (llvm::gep_type_iterator i = gep_type_begin(GEP),
		     e = gep_type_end(GEP); i != e; ++i, ++op) {
			llvm::Value *V = i.getOperand();
			if (llvm::isa<llvm::ConstantInt>(V))
				continue;
			IndexRanges.insert(std::make_pair(op, getIndexRange(*i, V)));
		}
	}

	llvm::ConstantRange getIndexRange(const llvm::Type *T, llvm::Value *V) {
		unsigned n;
		switch (T->getTypeID()) {
		default:
			T->dump();
			llvm_unreachable("Unknown type!");
		case llvm::Type::ArrayTyID:
			n = (unsigned)llvm::cast<llvm::ArrayType>(T)->getNumElements();
			break;
		case llvm::Type::VectorTyID:
			n = llvm::cast<llvm::VectorType>(T)->getNumElements();
			break;
		}
		unsigned BitWidth = V->getType()->getPrimitiveSizeInBits();
		llvm::ScalarEvolution &SE = getAnalysis<llvm::ScalarEvolution>();
		const llvm::SCEV *S = SE.getSCEV(V);
		llvm::ConstantRange R = SE.getUnsignedRange(S);
		if (R.isFullSet()) {
			if (llvm::PHINode *PN = llvm::dyn_cast<llvm::PHINode>(V)) {
				ValueSet VS;
				PHISet Visited;
				collectPHIs(PN, VS, Visited);
				R = llvm::ConstantRange(BitWidth, false);
				for (ValueSet::iterator i = VS.begin(), e = VS.end(); i != e; ++i) {
					const llvm::SCEV *S = SE.getSCEV(*i);
					R = R.unionWith(SE.getUnsignedRange(S));
					if (R.isFullSet())
						break;
				}
			}
		}
		R = R.intersectWith(llvm::ConstantRange(
			llvm::APInt(BitWidth, 0),
			llvm::APInt(BitWidth, n)
		));
		assert(R.getSetSize().sle(n));
		assert(!R.isWrappedSet());
		return R;
	}

	void collectPHIs(llvm::PHINode *PN, ValueSet &VS, PHISet &Visited) {
		if (!Visited.insert(PN))
			return;
		for (unsigned i = 0, n = PN->getNumIncomingValues(); i != n; ++i) {
			llvm::Value *V = PN->getIncomingValue(i);
			if (llvm::PHINode *SubPN = llvm::dyn_cast<llvm::PHINode>(V))
				collectPHIs(SubPN, VS, Visited);
			else
				VS.insert(V);
		}
	}

	void rewrite(llvm::Use *U, const llvm::ConstantRange &R,
	             llvm::GetElementPtrInst *GEP, llvm::LoadInst *I) {
		llvm::LLVMContext &Ctx = I->getContext();
		llvm::Function *F = I->getParent()->getParent();
		llvm::Value *V = *U;
		assert(!llvm::isa<llvm::ConstantInt>(V));
		llvm::IntegerType *IntTy = llvm::cast<llvm::IntegerType>(V->getType());
		unsigned n = (unsigned)R.getSetSize().getZExtValue();

		const llvm::Twine &Postfix = I->getName() + "." + V->getName();
		llvm::BasicBlock *OldBB = I->getParent(), *PhiBB, *NewBB;
		PhiBB = OldBB->splitBasicBlock(I, OldBB->getName() + "." + Postfix + ".phi");
		NewBB = PhiBB->splitBasicBlock(I, OldBB->getName() + "." + Postfix);
		// NewBB contains everything starting from the load.
		assert(NewBB->getFirstNonPHI() == I);
		// PhiBB contains only a br to newBB.
		assert(PhiBB->size() == 1);
		// Create a unreachable default.
		llvm::BasicBlock *DefaultBB = llvm::BasicBlock::Create(
			Ctx, "bb." + Postfix + ".default", F, PhiBB);
		new llvm::UnreachableInst(Ctx, DefaultBB);
		// OldBB must end with a br to PhiBB; replace it with switch.
		OldBB->getTerminator()->eraseFromParent();
		llvm::SwitchInst *SI = llvm::SwitchInst::Create(V, DefaultBB, n, OldBB);
		// Insert a phi here for merged loads.
		llvm::PHINode *PN = llvm::PHINode::Create(
			I->getType(), n, Postfix + ".merge", PhiBB->begin()
		);
		for (uint64_t k = R.getLower().getZExtValue();
			 k != R.getUpper().getZExtValue(); ++k) {
			llvm::ConstantInt *Idx = llvm::ConstantInt::get(IntTy, k);
			llvm::BasicBlock *CaseDest = llvm::BasicBlock::Create(
				Ctx, "bb." + Postfix + "." + llvm::Twine(k), F, PhiBB);
			llvm::GetElementPtrInst *CaseGEP = llvm::cast<
				llvm::GetElementPtrInst>(GEP->clone());
			CaseGEP->setOperand(U - GEP->op_begin(), Idx);
			CaseGEP->setName(GEP->getName() + Postfix + "." + llvm::Twine(k));
			llvm::LoadInst *CaseLoad = llvm::cast<
				llvm::LoadInst>(I->clone());
			CaseLoad->setOperand(0, CaseGEP);
			CaseLoad->setName(Postfix + "." + llvm::Twine(k));
			CaseLoad->insertBefore(
				llvm::BranchInst::Create(PhiBB, CaseDest)
			);
			CaseGEP->insertBefore(CaseLoad);
			SI->addCase(Idx, CaseDest);
			PN->addIncoming(CaseLoad, CaseDest);
			// Add other non-constant indices in the GEP to IndexRanges.
			for (llvm::Instruction::op_iterator i = CaseGEP->idx_begin(),
			     e = CaseGEP->idx_end(); i != e; ++i) {
				if (llvm::isa<llvm::ConstantInt>(i)) {
					IndexRangeMap::iterator ri = IndexRanges.find(i);
					if (ri != IndexRanges.end())
						IndexRanges.insert(std::make_pair(i, ri->second));
				}
			}
		}
		I->replaceAllUsesWith(PN);
		I->eraseFromParent();
	}
};

} // anonymous namespace

char GEPUnroll::ID;

static llvm::RegisterPass<GEPUnroll>
X("gepunroll", "Unroll GEPs in constant loads");
