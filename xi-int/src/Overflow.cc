#define DEBUG_TYPE "overflow"
#include <llvm/Instructions.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Metadata.h>
#include <llvm/Pass.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Support/PatternMatch.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/Debug.h>
#include <iostream>

using std::cerr;
using namespace llvm::PatternMatch;

namespace {

/// This pass recognizes overflow idioms and generates overflow intrinsics.
struct Overflow : llvm::FunctionPass {
	static char ID;
	Overflow() : llvm::FunctionPass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
	}

	virtual bool doInitialization(llvm::Module &M) {
		this->M = &M;
		return false;
	}

	virtual bool runOnFunction(llvm::Function &);

private:
	llvm::Module *M;

	// This step recognizes overflow checking idioms via overflows.
	// It also rewrites checks without overflows for better performance.
	bool visitICmpInst(llvm::ICmpInst *);
	llvm::CallInst *visitICmp(llvm::CmpInst::Predicate,
	                       llvm::Value *L, llvm::Value *R);

	llvm::CallInst *createOverflow(llvm::Intrinsic::ID id,
			llvm::Value *L, llvm::Value *R,
			const llvm::Twine &Name = "") {
		llvm::Function *F = llvm::Intrinsic::getDeclaration(M, id, L->getType());
		llvm::Value *Args[] = {L, R};
		return llvm::CallInst::Create(F, Args, Name);
	}

	llvm::CallInst *createUAddOverflow(llvm::Value *L, llvm::Value *R) {
		return createOverflow(llvm::Intrinsic::uadd_with_overflow, L, R, "uadd");
	}

	llvm::CallInst *createUSubOverflow(llvm::Value *L, llvm::Value *R) {
		return createOverflow(llvm::Intrinsic::usub_with_overflow, L, R, "usub");
	}

	llvm::CallInst *createUMulOverflow(llvm::Value *L, llvm::Value *R) {
		return createOverflow(llvm::Intrinsic::umul_with_overflow, L, R, "umul");
	}

	void markOverflow(llvm::Value *V) {
		if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V))
			I->setMetadata("ovf", llvm::MDNode::get(V->getContext(), 0));
	}

	bool visitBinaryOperator(llvm::BinaryOperator *);

	bool visitStoreInst(llvm::StoreInst *);

	bool visitTruncInst(llvm::TruncInst *);
};

} // anonymous namespace

bool Overflow::runOnFunction(llvm::Function &F) {
	//asm("int $3");
//	cerr << "Overflow::runOnFunction"  <<  "\n";

	cerr <<  "dbg:" << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " \n";
	
	bool Changed = false;
	llvm::inst_iterator i = inst_begin(F), e = inst_end(F);
	for (; i != e; ) {
		llvm::Instruction *I = &*i;
		++i;
		if (llvm::ICmpInst *ICI = llvm::dyn_cast<llvm::ICmpInst>(I))
			Changed |= visitICmpInst(ICI);
		else if (llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(I))
			Changed |= visitBinaryOperator(BO);
		else if (llvm::TruncInst *TI = llvm::dyn_cast<llvm::TruncInst>(I))
			Changed |= visitTruncInst(TI);
		else if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(I))
			Changed |= visitStoreInst(SI);
	}
	return Changed;
}

bool Overflow::Overflow::visitICmpInst(llvm::ICmpInst *I) {
	cerr <<  "dbg:" << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " \n";
	llvm::Value *L = I->getOperand(0), *R = I->getOperand(1);

	// Drity hack: ((x * c) cmp y) || (x > umax / c)
	llvm::Value *X, *Y, *Z;
	const llvm::APInt *C0, *C1;
	if (I->getPredicate() == llvm::ICmpInst::ICMP_UGT
	     && match(L, m_Value(X))
	     && match(R, m_APInt(C1))) {
		unsigned BitWidth = C1->getBitWidth();
		llvm::APInt UMax = llvm::APInt::getAllOnesValue(BitWidth);
		llvm::BasicBlock *BB = I->getParent();
		if (llvm::BasicBlock *Pred = BB->getSinglePredecessor()) {
			llvm::ICmpInst::Predicate IPred;
			llvm::BasicBlock *TrueDest, *FalseDest;
			llvm::Value *Mul;
			if (match(Pred->getTerminator(), m_Br(
				m_ICmp(IPred, m_Value(Mul), m_Value(Y)
				), TrueDest, FalseDest))
			    && match(Mul, m_Mul(m_Specific(X), m_APInt(C0)))
			    && FalseDest == BB
			    && *C0 == UMax.udiv(*C1)) {
				markOverflow(Mul);
				return true;
			}
		}
	}

	bool Changed = false;

        // (zext x) / (zext y) != (zext z)
        if (I->isEquality()
            && match(L, m_UDiv(m_ZExt(m_Value(X)), m_ZExt(m_Value(Y))))
            && match(R, m_ZExt(m_Value(Z)))
	    && X->getType() == Y->getType()
	    && X->getType() == Z->getType()) {
		llvm::BinaryOperator *NewL = llvm::BinaryOperator::CreateUDiv(X, Y, L->getName(), I);
		NewL->setDebugLoc(llvm::cast<llvm::Instruction>(L)->getDebugLoc());
                llvm::ICmpInst *NewInst = new llvm::ICmpInst(I->getPredicate(), NewL, Z);
		NewInst->setDebugLoc(I->getDebugLoc());
		llvm::ReplaceInstWithInst(I, NewInst);
		I = NewInst;
		L = I->getOperand(0);
		R = I->getOperand(1);
		Changed = true;
        }

	llvm::CallInst *CI = visitICmp(I->getPredicate(), L, R);
	// Check twice, with operands swapped.
	if (!CI && !I->isEquality())
		CI = visitICmp(I->getSwappedPredicate(), R, L);
	if (!CI)
		return Changed;
	CI->insertBefore(I);
	llvm::ReplaceInstWithInst(I,
		llvm::ExtractValueInst::Create(CI, 1));
	return true;
}

llvm::CallInst *Overflow::visitICmp(llvm::CmpInst::Predicate Pred,
                                    llvm::Value *L, llvm::Value *R) {
	cerr <<  "dbg:" << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " \n";
	
	llvm::Value *X, *Y, *Z, *W, *A, *B;
	// Overflow checking via overflows.
	// x + y <u x
	// x + y <u y
	if ((Pred == llvm::CmpInst::ICMP_ULT || Pred == llvm::CmpInst::ICMP_ULE)
	    && match(L, m_Add(m_Value(X), m_Value(Y)))
	    && (R == X || R == Y)) {
		markOverflow(L);
		return createUAddOverflow(X, Y);
	}
	// ((x + y) >> z) < (x >> z)
	// ((x + y) >> z) < (y >> z)
	if (Pred == llvm::CmpInst::ICMP_ULT
	    && match(L, m_LShr(m_Add(m_Value(X), m_Value(Y)), m_Value(Z)))
	    && match(R, m_LShr(m_Value(A), m_Specific(Z)))
	    && (A == X || A == Y)) {
		markOverflow(L);
		return createUAddOverflow(X, Y);
	}
	// ((x + y + z) >> w) < (x >> w)
	if (Pred == llvm::CmpInst::ICMP_ULT
	    && match(L, m_LShr(m_Value(A), m_Value(W)))
	    && match(A, m_Add(m_Value(B), m_Value(Z)))
	    && match(B, m_Add(m_Value(X), m_Value(Y)))
	    && match(R, m_LShr(m_Specific(X), m_Specific(W)))) {
		markOverflow(A);
		markOverflow(B);
		return 0;
	}
	// x - y <s 0
	if (Pred == llvm::CmpInst::ICMP_SLT
	    && match(L, m_Sub(m_Value(X), m_Value(Y)))
	    && match(R, m_Zero())) {
		markOverflow(L);
		return createUSubOverflow(X, Y);
	}
	// trunc(x - y) <s 0
	if (Pred == llvm::ICmpInst::ICMP_SLT
	    && match(L, m_Trunc(m_Value(A)))
	    && match(A, m_Sub(m_Value(X), m_Value(Y)))
	    && match(R, m_Zero())) {
		markOverflow(A);
		return 0;
	}
	// (x * y) / x != y
	if ((Pred == llvm::CmpInst::ICMP_NE || Pred == llvm::CmpInst::ICMP_EQ)
	    && match(L, m_UDiv(m_Value(A), m_Value(Z)))
	    && match(R, m_Value(W))
	    && match(A, m_Mul(m_Value(X), m_Value(Y)))
	    && ((Z == X && W == Y) || (Z == Y && W == X))) {
		markOverflow(A);
		return createUMulOverflow(X, Y);
	}
	// (x * y * z) / (y * z) != x
	if ((Pred == llvm::CmpInst::ICMP_NE || Pred == llvm::CmpInst::ICMP_EQ)
	    && match(L, m_UDiv(m_Value(A), m_Value(B)))
	    && match(A, m_Mul(m_Mul(m_Value(X), m_Value(Y)), m_Value(Z)))
	    && match(B, m_Mul(m_Specific(Y), m_Specific(Z)))
	    && match(R, m_Specific(X))) {
                markOverflow(A);
                return createUMulOverflow(B, X);
	}
        // (x * y * z) / (x * z) != y
        if ((Pred == llvm::CmpInst::ICMP_NE || Pred == llvm::CmpInst::ICMP_EQ)
	    && match(L, m_UDiv(m_Value(A), m_Value(B)))
	    && match(A, m_Mul(m_Mul(m_Value(X), m_Value(Y)), m_Value(Z)))
	    && match(B, m_Mul(m_Specific(X), m_Specific(Z)))
	    && match(R, m_Specific(Y))) {
                markOverflow(A);
                return createUMulOverflow(B, Y);
	}

	// Overflow checks without overflows; rewrite for better performance.
	// umax - x <u y
	if (Pred == llvm::CmpInst::ICMP_ULT
	    && match(L, m_Sub(m_AllOnes(), m_Value(X)))) {
		markOverflow(L);
		return createUAddOverflow(X, R);
	}
	// umax / x <u y
	if (Pred == llvm::CmpInst::ICMP_ULT
	    && match(L, m_UDiv(m_AllOnes(), m_Value(X)))) {
		markOverflow(L);
		return createUMulOverflow(X, R);
	}

	return 0;
}

bool Overflow::visitBinaryOperator(llvm::BinaryOperator *I) {
	cerr <<  "dbg:" << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " \n";
	llvm::Value *X;
	const llvm::APInt *C0, *C1;
	llvm::Type *T = I->getType();
	llvm::BinaryOperator *BO = 0;
	// x + c0 + c1
	if (match(I, m_Add(m_Add(m_Value(X), m_APInt(C0)), m_APInt(C1))))
		BO = llvm::BinaryOperator::CreateAdd(X,
			llvm::ConstantInt::get(T, *C0 + *C1));
	// x + c0 - c1
	else if (match(I, m_Sub(m_Add(m_Value(X), m_APInt(C0)), m_APInt(C1))))
		BO = llvm::BinaryOperator::CreateAdd(X,
			llvm::ConstantInt::get(T, *C0 - *C1));
	// Clang generates add x, -1 for --x
	else if (match(I, m_Add(m_Value(X), m_AllOnes())))
		BO = llvm::BinaryOperator::CreateSub(X,
			llvm::ConstantInt::get(T, 1));
#if 0
	// (x + (c - 1)) / c
	else if (match(I, m_UDiv(m_Value(A), m_APInt(C1)))
	         && match(A, m_Add(m_Value(X), m_APInt(C0)))
	         && *C0 == *C1 - llvm::APInt(C1->getBitWidth(), 1))
		markOverflow(A);
	// ((x + (2**c - 1)) >> c) - (y >> c)
	else if (match(I, m_Sub(m_Value(B), m_LShr(m_Value(Y), m_APInt(C2))))
	         && match(B, m_LShr(m_Value(A), m_APInt(C1)))
	         && match(A, m_Add(m_Value(X), m_APInt(C0)))
		 && *C1 == *C2
	         && *C1 == (*C0 + llvm::APInt(C1->getBitWidth(), 1)).logBase2())
		markOverflow(A), markOverflow(I);
	// (... + x - 1) & ~(x - 1)
	else if (match(I, m_And(m_Value(B), m_Xor(m_Sub(m_Value(X), m_One()), m_AllOnes())))
	         && match(B, m_Sub(m_Value(A), m_One())))
		markOverflow(A), markOverflow(B);
#endif
	if (!BO)
		return false;
	if (I->hasNoSignedWrap())
		BO->setHasNoSignedWrap();
	BO->setDebugLoc(I->getDebugLoc());
	llvm::ReplaceInstWithInst(I, BO);
	return true;
}

bool Overflow::visitStoreInst(llvm::StoreInst *I) {
	cerr <<  "dbg:" << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " \n";
	llvm::Value *P = I->getPointerOperand()->stripPointerCasts();
	llvm::Value *V = I->getValueOperand();
	llvm::Value *X;
	// Ignore ++a.x (inc on fields) for now. They are mostly stats.
	// We should have a whitelist.
	if (I->getMetadata("id") && match(V, m_Add(m_Value(X), m_Constant()))) {
		if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(X)) {
			if (LI->getPointerOperand()->stripPointerCasts() == P) {
				markOverflow(V);
				return true;
			}
		}
	}
	return false;
}

// For operations like z = x + y of type char/short, x and y will be first
// promoted to int and truncated back later.
//   a = ext x
//   b = ext y
//   c = a op b
//   z = trunc c
bool Overflow::visitTruncInst(llvm::TruncInst *I) {
	cerr <<  "dbg:" << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " \n";
	llvm::Value *X;
	const llvm::APInt *C;
	llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(I->getOperand(0));
	if (!BO)
		return false;
	llvm::Value *L = BO->getOperand(0), *R = BO->getOperand(1);
	llvm::Type *T = I->getType();
	unsigned BitWidth = T->getPrimitiveSizeInBits();
	llvm::Instruction *NewInstr = 0;
	if (match(L, m_ZExt(m_Value(X)))
	    && X->getType() == T
	    && match(R, m_APInt(C)))
		NewInstr = llvm::BinaryOperator::Create(
			BO->getOpcode(),
			X,
			llvm::ConstantInt::get(T, C->trunc(BitWidth))
		);
	if (!NewInstr)
		return false;
	NewInstr->setDebugLoc(BO->getDebugLoc());
	llvm::ReplaceInstWithInst(I, NewInstr);
	return true;
}

char Overflow::ID;

static llvm::RegisterPass<Overflow>
X("overflow", "Recognize overflow idioms");

llvm::Pass *createOverflowPass() {
	cerr <<  "dbg:" << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " \n";
	return new Overflow;
}
