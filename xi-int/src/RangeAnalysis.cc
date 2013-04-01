#include "AnnotationManager.h"
#include "VRMap.h"
#include "VRGraph.h"
#include "IntRange.h"
#include "RangeWorkList.h"
#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Constants.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/InstVisitor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CFG.h"
#include <map>
#include "stdio.h"

using std::cerr;
/*static llvm::cl::opt<std::string>
AnnotationFileName("ranges-annotation", llvm::cl::init(""), llvm::cl::Hidden,
			llvm::cl::desc("Specify the annotation file for Value Range Analysis."));*/

static llvm::cl::opt<bool>
VerboseOutput("ranges-verbose-output", llvm::cl::init(false), llvm::cl::Hidden,
            llvm::cl::desc("Enable verbose output for unit test."));

namespace {

struct RangeAnalysis : VRGraph, llvm::ModulePass, llvm::InstVisitor<RangeAnalysis, void> {
    static char ID;
    RangeAnalysis() : llvm::ModulePass(ID) {
        llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry(); 
        llvm::initializeLoopInfoPass(Registry);
        this->verbose = VerboseOutput;
        this->LoopThreshold = 10;
        this->ExpandThreshold = 20;
        this->GlobalThreshold = 40;
        WorkList = NULL;
		//AManager = NULL;
/*		if (AnnotationFileName != "") { 
			AManager = new AnnotationManager(AnnotationFileName.getValue().c_str());
			VR = VRMap(AManager);
		}*/
    }

    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
        AU.setPreservesAll();
        AU.addRequired<llvm::LoopInfo>();
    }

    virtual void *getAdjustedAnalysisPointer(llvm::AnalysisID ID) {
        if (ID == &VRGraph::ID)
            return static_cast<VRGraph *>(this);
        return static_cast<llvm::ModulePass *>(this);
    }

    virtual bool runOnModule(llvm::Module &M);

    virtual IntRange getValueRange(llvm::Value* V) {
        llvm::ConstantInt *CI;
        if ((CI = llvm::dyn_cast<llvm::ConstantInt>(V))) 
            return IntRange(CI->getValue());
        return VR.getValueRange(V);
    }

    IntRange getValueRangeInBB(llvm::Value* V, llvm::BasicBlock* BB) {
        IntRange ret = getValueRange(V);
        if (! llvm::isa<llvm::ConstantInt>(V))
            if (PathConstraints.count(BB) != 0)
                ret = ret.intersectWith(PathConstraints[BB].getValueRange(V, true));
        return ret;
    }

    void visitInstruction(llvm::Instruction &I) {
        llvm::errs() << "Unhandled Instruction:" ;
        I.dump();
        assert(0);
    }

    void visitTerminatorInst(llvm::TerminatorInst &I) {
        llvm::ReturnInst *RI;
        if ((RI = llvm::dyn_cast<llvm::ReturnInst>(&I))) {
            if (RI->getNumOperands() == 0) return;
            llvm::Value *V = RI->getOperand(0);
            if (V->getType()->isIntegerTy()) {
                llvm::Function *F = llvm::dyn_cast<llvm::Function>(I.getParent()->getParent());
                if (setValueRange(F, getValueRangeInBB(V, I.getParent())))
                    WorkList->update(F);
            }
        }
    }

    void visitCallInst(llvm::CallInst &I);

    void visitLoadInst(llvm::LoadInst &I) {
        // Ignore load inst, will be handled by ValueNode constructor
    }

    void visitStoreInst(llvm::StoreInst &I) {
        if (I.getValueOperand()->getType()->isIntegerTy())
            if (setValueRange(&I, getValueRangeInBB(I.getValueOperand(), I.getParent())))
                WorkList->update(&I);
    }

	void visitSelectInst(llvm::SelectInst &I) {
		VRMap TrueMap = runOnCondition(I.getCondition(), true);
		VRMap FalseMap = runOnCondition(I.getCondition(), false);
		IntRange TrueRange = getValueRangeInBB(I.getTrueValue(), I.getParent()).intersectWith(TrueMap.getValueRange(I.getTrueValue(), true));
		IntRange FalseRange = getValueRangeInBB(I.getFalseValue(), I.getParent()).intersectWith(FalseMap.getValueRange(I.getFalseValue(), true));
		if (setValueRange(&I, TrueRange.unionWith(FalseRange)))
			WorkList->update(&I);
	}

    void visitPtrToIntInst(llvm::PtrToIntInst &I) {
        llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I.getType());
        assert(T && "Must be integer type");
        if (setValueRange(&I, IntRange(T->getBitWidth(), true)))
            WorkList->update(&I);
    }

    void visitPHINode(llvm::PHINode &I) {
        llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I.getType());
        assert(T && "Must be integer type");
        IntRange tmp(T->getBitWidth(), false);
        for (size_t i = 0; i < I.getNumOperands(); i++) {
            VRMap cond = runOnTerminator(I.getIncomingBlock(i), I.getParent());
            IntRange r = getValueRangeInBB(I.getOperand(i), I.getIncomingBlock(i)).intersectWith(cond.getValueRange(I.getOperand(i), true));
            tmp = tmp.unionWith(r);
        }
        if (setValueRange(&I, tmp))
            WorkList->update(&I);
    }

    void visitTruncInst(llvm::TruncInst &I) {
        llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I.getType());
        assert(T && "Must be integer type");
        if (setValueRange(&I, getValueRangeInBB(I.getOperand(0), I.getParent()).truncate(T->getBitWidth())))
            WorkList->update(&I);
    }

    void visitZExtInst(llvm::ZExtInst &I) {
        llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I.getType());
        assert(T && "Must be integer type");
        if (setValueRange(&I, getValueRangeInBB(I.getOperand(0), I.getParent()).zeroExtend(T->getBitWidth())))
            WorkList->update(&I);
    }

    void visitSExtInst(llvm::SExtInst &I) {
        llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I.getType());
        assert(T && "Must be integer type");
        if (setValueRange(&I, getValueRangeInBB(I.getOperand(0), I.getParent()).signExtend(T->getBitWidth())))
            WorkList->update(&I);
    }

	void visitExtractValueInst(llvm::ExtractValueInst &I) {
		llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I.getType());
		assert(T && "Must be integer type");
		// TODO: fix this
		if (setValueRange(&I, IntRange(T->getBitWidth(), I.getType())))
			WorkList->update(&I);
	}

    void visitBinaryOperator(llvm::BinaryOperator &I);

    void visitCmpInst(llvm::CmpInst &I) {
        //TODO: more precise
        llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I.getType());
        assert(T && "Must be integer type");
        if (setValueRange(&I, IntRange(T->getBitWidth(), true)))
            WorkList->update(&I);
    }

private:
    typedef std::map<llvm::BasicBlock*, VRMap> PathConstraintsTy;
    VRMap VR;
    PathConstraintsTy PathConstraints;
    bool verbose;
    RangeWorkList *WorkList;
//	AnnotationManager *AManager;
    unsigned int LoopThreshold;
    unsigned int ExpandThreshold;
    unsigned int GlobalThreshold;
    std::map<llvm::Value*, unsigned int> updateCnt;
	std::set<llvm::BasicBlock*> Reachable;

    struct APIntCmp {
        bool operator () (const llvm::APInt &A, const llvm::APInt &B) const {
            return A.slt(B);
        }
    };

    bool setValueRange(llvm::Value *V, IntRange R) {
        unsigned int cnt = updateCnt[V];
        if (cnt > GlobalThreshold)
            return VR.setValueRange(V, IntRange(R.getBitWidth(), true));
        if (cnt > LoopThreshold && llvm::isa<llvm::Instruction>(V) && ! llvm::isa<llvm::StoreInst>(V)) {
            llvm::BasicBlock *BB = llvm::dyn_cast<llvm::Instruction>(V)->getParent();
            llvm::LoopInfo *LI = &(this->getAnalysis<llvm::LoopInfo>(*(BB->getParent())));
            llvm::Loop *L = LI->getLoopFor(BB);
            if (L) {
                // FIXME: Some dirty and ugly work for LOOPs
                typedef std::set<llvm::APInt, APIntCmp> APIntSet;
                std::set<llvm::APInt, APIntCmp> lowerBounds, upperBounds;
                for (size_t i = 0; i < L->getBlocks().size(); i++) {
                    llvm::BasicBlock *BB1 = L->getBlocks()[i];
                    IntRange R1 = IntRange(R.getBitWidth(), true);
                    if (PathConstraints.count(BB1) != 0)
                        R1 = PathConstraints[BB1].getValueRange(V);
                    upperBounds.insert(R1.getSignedMax());
                    lowerBounds.insert(R1.getSignedMin());
                }
                IntRange OldValue = getValueRange(V);
                if (R.getSignedMax().sgt(OldValue.getSignedMax())) {
                    APIntSet::iterator it = upperBounds.upper_bound(R.getSignedMax());
                    if (it != upperBounds.end() && ((cnt > ExpandThreshold) || (*it != llvm::APInt::getSignedMaxValue(R.getBitWidth())))) {
                        R = R.unionWith(IntRange(R.getSignedMax(), *it + 1));
                        updateCnt[V] = 0;
                    }
                }
                if (R.getSignedMin().slt(OldValue.getSignedMin())) {
                    APIntSet::iterator it = lowerBounds.lower_bound(R.getSignedMin());
                    if (it != lowerBounds.begin() && ((cnt > ExpandThreshold) || (*it != llvm::APInt::getSignedMinValue(R.getBitWidth())))) {
                        it --;
                        R = R.unionWith(IntRange(*it, R.getSignedMin()));
                        updateCnt[V] = 0;
                    }
                }
			}
        }
        bool ret = VR.setValueRange(V, R);
        if (ret) updateCnt[V] ++;
        return ret;
    }

    void InitializeGraph(llvm::Module &M) {
        PathConstraints.clear();
        VR.clear();
        updateCnt.clear();
        //FIXME: now assume all external function can be called outside
        for (llvm::Module::iterator it = M.begin(); it != M.end(); it ++) {
            llvm::Function *F = &*it;
            if (llvm::GlobalValue::isExternalLinkage(F->getLinkage()) && ! F->isDeclaration()) { 
                // Assume its argument can take full set
                for (llvm::Function::arg_iterator ait = F->arg_begin(); ait != F->arg_end(); ait ++) {
                    llvm::Argument *A = &*ait;
                    if (A->getType()->isIntegerTy()) {
                        llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(A->getType());
                        setValueRange(A, IntRange(T->getBitWidth(), true));
                    }
                }
            }
            // Assume return values of all external functions can take full set
            if (F->isDeclaration()) 
                if (F->getReturnType()->isIntegerTy()) {
                    llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(F->getReturnType());
                    setValueRange(F, IntRange(T->getBitWidth(), true));
                }
        }
    }
	
	VRMap runOnCondition(llvm::Value *V, bool TargetRes);

    VRMap runOnTerminator(llvm::BasicBlock *PredBB, llvm::BasicBlock *BB) {
        llvm::TerminatorInst *TI = PredBB->getTerminator();
        llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(TI);
        if (BI) {
            if (BI->isUnconditional())
                return VRMap();
			return runOnCondition(BI->getCondition(), BI->getSuccessor(0) == BB);
        }
        llvm::SwitchInst *SI = llvm::dyn_cast<llvm::SwitchInst>(TI);
        if (SI) {
            llvm::Value* SwitchValue = SI->getOperand(0);
            if ((! SwitchValue->getType()->isIntegerTy()) || (llvm::isa<llvm::Constant>(SwitchValue)))
                return VRMap();
            size_t idx;
            for (idx = 0; idx < SI->getNumSuccessors(); idx ++)
                if (BB == SI->getSuccessor(idx))
                    break;
            VRMap ret;
            if (idx != 0) {
                llvm::Value *V = SI->getSuccessorValue(idx);
                ret.setValueRange(SwitchValue, getValueRange(V));
            }
            return ret;
        }
        //TODO: more terminator cases
        return VRMap();
    }

    void runOnBasicBlockPredicate(llvm::BasicBlock *BB) {
        llvm::pred_iterator pit;
        VRMap NewConstraints;
        for (pit = llvm::pred_begin(BB); pit != llvm::pred_end(BB); pit ++) {
            llvm::BasicBlock *PredBB = *pit;
			VRMap C;
            if (PathConstraints.count(PredBB) != 0)
				C = PathConstraints[PredBB];
			C.appendConstraints(runOnTerminator(PredBB, BB));
			NewConstraints.unionWith(C);
        }
		llvm::BasicBlock::iterator iit;
		for (iit = BB->begin(); iit != BB->end(); iit ++) {
			llvm::Instruction *I = &*iit;
			if (! llvm::isa<llvm::StoreInst>(I) && ! llvm::isa<llvm::TerminatorInst>(I) && ! llvm::isa<llvm::LoadInst>(I)) {
				llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I->getType());
				if (T) 
					NewConstraints.setValueRange(I, IntRange(T->getBitWidth(), true));
			}
		}
		if (BB == &BB->getParent()->getEntryBlock()) {
			llvm::Function *F = BB->getParent();
			for (llvm::Function::arg_iterator it = F->arg_begin(); it != F->arg_end(); it++) {
				llvm::Argument *Arg = &*it;
				llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(Arg->getType());
				if (T)
					NewConstraints.setValueRange(Arg, IntRange(T->getBitWidth(), true));
			}
		}
		bool updated = false;
        if (PathConstraints.count(BB) == 0) {
            WorkList->update(BB);
			updated = true;
            PathConstraints[BB] = NewConstraints;
        }
        else if (NewConstraints != PathConstraints[BB]) {
            WorkList->update(BB);
			updated = true;
            PathConstraints[BB] = NewConstraints;
        }
		if (NewConstraints.checkAgainst(VR))
			if (Reachable.count(BB) == 0) {
				Reachable.insert(BB);
				if (!updated) WorkList->update(BB);
			}
    }

	IntRange runOnAnnotationTerm(llvm::CallInst *CI, AnnotationTerm Term) {
		llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(CI->getType());
		IntRange ret(llvm::APInt(T->getBitWidth(), 1), llvm::APInt(T->getBitWidth(), 2));
		for (size_t i = 0; i < Term.size(); i ++) {
			AnnotationExpValue V = Term[i];
			if (V.Kind == AnnotationExpValue::IntKind) {
				IntRange tmp = IntRange(llvm::APInt(T->getBitWidth(), V.D), llvm::APInt(T->getBitWidth(), V.D+1));
				ret = ret.multiply(tmp);
			}
			else {
				llvm::Value *Arg = CI->getArgOperand(V.D);
				assert(Arg && "Not enough arguments!");
				ret = ret.multiply(getValueRangeInBB(Arg, CI->getParent()));
			}
		}
		return ret;
	}

	IntRange runOnAnnotationExp(llvm::CallInst *CI, AnnotationExp E) {
		llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(CI->getType());
		IntRange ret(llvm::APInt(T->getBitWidth(), 0), llvm::APInt(T->getBitWidth(), 1));
		for (size_t i = 0; i < E.Terms.size(); i++) {
			if (E.Signs[i]) 
				ret = ret.add(runOnAnnotationTerm(CI, E.Terms[i]));
			else
				ret = ret.sub(runOnAnnotationTerm(CI, E.Terms[i]));
		}
		return ret;
	}
};

IntRange OperateRange(int opcode, IntRange r1, IntRange r2) {
	switch (opcode) {
		case llvm::Instruction::Add:
			return r1.add(r2);
		case llvm::Instruction::Sub:
			return r1.sub(r2);
		case llvm::Instruction::Mul:
			return r1.multiply(r2);
		case llvm::Instruction::UDiv:
			return r1.udiv(r2);
		case llvm::Instruction::SDiv:
			return r1.sdiv(r2);
		case llvm::Instruction::URem:
			return r1.urem(r2);
		case llvm::Instruction::SRem:
			return r1.srem(r2);
		case llvm::Instruction::Shl:
			return r1.shl(r2);
		case llvm::Instruction::LShr:
			return r1.lshr(r2);
		case llvm::Instruction::AShr:
			return r1.ashr(r2);
		case llvm::Instruction::And:
			return r1.binaryAnd(r2);
		case llvm::Instruction::Or:
			return r1.binaryOr(r2);
		case llvm::Instruction::Xor:
			return r1.binaryXor(r2);
		default:
			llvm::errs() << "Unhandled binary opcode " << opcode << ".\n";
			assert(0);
	}
	return IntRange(r1.getBitWidth(), true);
}

void SetRegionConstraints(VRMap &M, llvm::BasicBlock *BB, llvm::Value *V, IntRange R) {
	M.setValueRange(V, R);
	for (llvm::Value::use_iterator it = V->use_begin(); it != V->use_end(); it ++) {
		llvm::User *U = *it;
		llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(U);
		if (I)
			if (I->getParent() == BB) {
				llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(U->getType());
				if (T) {
					if (llvm::isa<llvm::ZExtInst>(U)) 
						SetRegionConstraints(M, BB, U, R.zeroExtend(T->getBitWidth()));
					else if (llvm::isa<llvm::SExtInst>(U))
						SetRegionConstraints(M, BB, U, R.signExtend(T->getBitWidth()));
					else if (llvm::isa<llvm::BinaryOperator>(U)) {
						if (I->getOperand(0) == V && llvm::isa<llvm::ConstantInt>(I->getOperand(1)))
							SetRegionConstraints(M, BB, U, OperateRange(I->getOpcode(), R, IntRange(llvm::dyn_cast<llvm::ConstantInt>(I->getOperand(1))->getValue())));
						else if (I->getOperand(1) == V && llvm::isa<llvm::ConstantInt>(I->getOperand(0)))
							SetRegionConstraints(M, BB, U, OperateRange(I->getOpcode(), R, IntRange(llvm::dyn_cast<llvm::ConstantInt>(I->getOperand(0))->getValue())));
					}
				}
			}
	}
}

} // anonymous namespace

char RangeAnalysis::ID;

void RangeAnalysis::visitBinaryOperator(llvm::BinaryOperator &I) {
	llvm::Value *op1 = I.getOperand(0);
	llvm::Value *op2 = I.getOperand(1);
	IntRange r1 = getValueRangeInBB(op1, I.getParent());
	IntRange r2 = getValueRangeInBB(op2, I.getParent());
	IntRange result = OperateRange(I.getOpcode(), r1, r2);
	if (setValueRange(&I, result))
		WorkList->update(&I);
}

void RangeAnalysis::visitCallInst(llvm::CallInst &I) {
	llvm::Function *F = I.getCalledFunction();
	// TODO: function pointer
	//assert(F && "Unhandled function pointer!");
	if (!F) return;
	if (I.getType()->isIntegerTy()) {
		bool found = false;
/*		if (AManager) {
			std::pair<AnnotationExp, AnnotationExp> p;
			if (AManager->getFunctionExp(F->getName(), p)) {
				found = true;
				if (setValueRange(&I, IntRange(runOnAnnotationExp(&I, p.first).getLower(), runOnAnnotationExp(&I, p.second).getUpper())))
					WorkList->update(&I);
			}
		}*/
		if (!found)
			if (setValueRange(&I, getValueRange(F)))
				WorkList->update(&I);
	}
	if (F->isDeclaration())
		return;
	size_t i = 0;
	for (llvm::Function::arg_iterator ait = F->arg_begin(); ait != F->arg_end(); ait ++, i ++) {
		llvm::Value *actual = I.getArgOperand(i);
		llvm::Argument *formal = &*ait;
		if (actual->getType()->isIntegerTy() && formal->getType()->isIntegerTy())
			if (setValueRange(formal, getValueRangeInBB(actual, I.getParent())))
				WorkList->update(formal);
	}
}


bool RangeAnalysis::runOnModule(llvm::Module &M) {
    WorkList = new RangeWorkList(M);
    InitializeGraph(M);
	Reachable.clear();
    if (verbose) 
        M.dump();

    while (!WorkList->isEmpty()) {
        llvm::Value *V = WorkList->deque();
        llvm::BasicBlock *BB = llvm::dyn_cast<llvm::BasicBlock>(V);
        if (BB) 
            runOnBasicBlockPredicate(BB);
        else {
            llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V);
            assert(I && "Unexpected worklist error.");
			if (Reachable.count(I->getParent()) != 0)
	            this->visit(*I);
        }
    }
    delete WorkList;

    if (verbose) {
        llvm::errs() << "\n[Result]\n";
        VR.dump();
    }
    return false;
}

VRMap RangeAnalysis::runOnCondition(llvm::Value *V, bool TargetRes) {
	// FIXME: Now we only handle the case of CmpInst, PHINode and BinaryOperator.
	// Not sure whether it is possible to have other kinds of llvm::value to be
	// condition in generated code.
	if (llvm::isa<llvm::CmpInst>(V)) {
		llvm::CmpInst *CI = llvm::dyn_cast<llvm::CmpInst>(V);
		llvm::Value* op1 = CI->getOperand(0);
		llvm::Value* op2 = CI->getOperand(1);
		if (! op1->getType()->isIntegerTy() || ! op2->getType()->isIntegerTy())
			return VRMap();
		llvm::CmpInst::Predicate pred, sw_pred;
		if (TargetRes)
			pred = CI->getPredicate(); 
		else
			pred = CI->getInversePredicate();
		sw_pred = llvm::CmpInst::getSwappedPredicate(pred);
		VRMap ret;
		if (! llvm::isa<llvm::Constant>(op1) && ! llvm::isa<llvm::LoadInst>(op1)) 
			SetRegionConstraints(ret, CI->getParent(), op1, llvm::ConstantRange::makeICmpRegion(pred, getValueRangeInBB(op2, CI->getParent())));
		if (! llvm::isa<llvm::Constant>(op2) && ! llvm::isa<llvm::LoadInst>(op2)) 
			SetRegionConstraints(ret, CI->getParent(), op2, llvm::ConstantRange::makeICmpRegion(sw_pred, getValueRangeInBB(op1, CI->getParent())));
		return ret;
	}
	else if (llvm::isa<llvm::BinaryOperator>(V)) {
		llvm::BinaryOperator *BI = llvm::dyn_cast<llvm::BinaryOperator>(V);
		assert( (BI->getOpcode() == llvm::Instruction::And || BI->getOpcode() == llvm::Instruction::Or) && "BinaryOperator should be either And or Or in conditions");
		VRMap ret;
		if (BI->getOpcode() == llvm::Instruction::And) {
			if (TargetRes) {
				ret = runOnCondition(BI->getOperand(0), true);
				ret.conditionAnd(runOnCondition(BI->getOperand(1), true));
			}
			else {
				ret = runOnCondition(BI->getOperand(0), false);
				ret.conditionOr(runOnCondition(BI->getOperand(1), false));
			}
		}
		else {
			if (TargetRes) {
				ret = runOnCondition(BI->getOperand(0), true);
				ret.conditionOr(runOnCondition(BI->getOperand(1), true));
			}
			else {
				ret = runOnCondition(BI->getOperand(0), false);
				ret.conditionAnd(runOnCondition(BI->getOperand(1), false));
			}
		}
		return ret;
	}
	else {
		llvm::PHINode *PI = llvm::dyn_cast<llvm::PHINode>(V);
		//FIXME: extractvalue cases generated by overflow check
		//assert(PI && "The condition is not CmpInst, PHINode and BinaryOperator.");
		if (!PI)
			return VRMap();
		VRMap ret;
		for (size_t i = 0; i < PI->getNumIncomingValues(); i++) {
			llvm::Value *V1 = PI->getIncomingValue(i);
			if (llvm::isa<llvm::ConstantInt>(V1)) {
				llvm::ConstantInt *C = llvm::dyn_cast<llvm::ConstantInt>(V1);
				if (C->isZero()==TargetRes)
					continue;
			}
			VRMap tmp = PathConstraints[PI->getIncomingBlock(i)];
			tmp.appendConstraints(runOnTerminator(PI->getIncomingBlock(i), PI->getParent()));
			tmp.appendConstraints(runOnCondition(V1, TargetRes));
			ret.unionWith(tmp);
		}
		return ret;
	}
}

static llvm::RegisterPass<RangeAnalysis> 
X("ranges", "Value Range Analysis", false, true);

static llvm::RegisterAnalysisGroup<VRGraph, true>
Y(X);

