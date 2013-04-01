#define DEBUG_TYPE "tautological-compare"
#include <llvm/Pass.h>
#include <llvm/Analysis/DebugInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Support/PatternMatch.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>


using namespace llvm::PatternMatch;
using std::cerr;

namespace {

struct TautologicalCompare : llvm::FunctionPass {
	static char ID;
	TautologicalCompare(llvm::raw_ostream *OS = &llvm::errs())
		: llvm::FunctionPass(ID), OS(*OS) {
		llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
		llvm::initializeScalarEvolutionPass(Registry);
	}

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesAll();
		AU.addRequired<llvm::ScalarEvolution>();
	}

	virtual bool runOnFunction(llvm::Function &F);

private:
	llvm::raw_ostream &OS;
	llvm::ScalarEvolution *SE;

	bool report(const llvm::Twine &, llvm::Instruction *);

	void warnConstCondition(llvm::ICmpInst *);

	void warnOverflowCheck(llvm::ICmpInst *);
	const char *warnOverflowCheck(llvm::CmpInst::Predicate, llvm::Value *L, llvm::Value *R);

	const char *computeCmp(
		llvm::CmpInst::Predicate Pred,
		const llvm::SCEV *L,
		const llvm::SCEV *R
	) {
		if (SE->isKnownPredicate(Pred, L, R))
			return "tautology";
		if (SE->isKnownPredicate(llvm::CmpInst::getInversePredicate(Pred), L, R))
			return "contradiction";
		return 0;
	}

	bool isKnownCmp(llvm::ICmpInst *);
};

} // anonymous namespace

bool TautologicalCompare::runOnFunction(llvm::Function &F) {
	cerr << "TautologicalCompare::runOnFunction"  <<  "\n";
	SE = &getAnalysis<llvm::ScalarEvolution>();
	for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
		if (llvm::ICmpInst *ICI = llvm::dyn_cast<llvm::ICmpInst>(&*i)) {
			warnConstCondition(ICI);
			warnOverflowCheck(ICI);
		}
	}
	return false;
}


bool TautologicalCompare::report(const llvm::Twine &Msg, llvm::Instruction *I) {
	const llvm::DebugLoc &DbgLoc = I->getDebugLoc();
	assert(!DbgLoc.isUnknown());
	llvm::MDNode *N = DbgLoc.getAsMDNode(I->getContext());
	llvm::DILocation Loc(N);
	assert(Loc.Verify());
	if (!Loc.getFilename().endswith(".c"))
		return false;	
	OS << "intmatch: " << Msg << " @"
	   << Loc.getFilename() << ':' << Loc.getLineNumber() << '\n'
	   << *I << '\n';
	return true;
}

void TautologicalCompare::warnConstCondition(llvm::ICmpInst *I) {
	// Only deal with integer types.
	if (!I->getOperand(0)->getType()->isIntegerTy())
		return;
	// Skip idioms.
	if (isKnownCmp(I))
		return;
	const llvm::SCEV *LHS = SE->getSCEV(I->getOperand(0));
	const llvm::SCEV *RHS = SE->getSCEV(I->getOperand(1));
	// Ignore trivial cases.
	if (LHS == RHS)
		return;
	// Ignore constant comparison.
	if (llvm::isa<llvm::SCEVConstant>(LHS) && llvm::isa<llvm::SCEVConstant>(RHS))
		return;
	// Ignore loop variables.
	if (llvm::isa<llvm::SCEVAddRecExpr>(LHS) || llvm::isa<llvm::SCEVAddRecExpr>(RHS))
		return;

	// Is known predicate?
	const char *Msg = computeCmp(I->getPredicate(), LHS, RHS);
	const char *Prefix = "";
	// Not known predicate.
	// Test if *LE/*GE is actually EQ/NE,
	// i.e., if >/< always/never holds in >=/<=.
	if (!Msg) {
		// Remove equality.
		llvm::CmpInst::Predicate APred;
		switch (I->getPredicate()) {
		default: return;
		case llvm::CmpInst::ICMP_SGE:
			APred = llvm::CmpInst::ICMP_SGT;
			Prefix = "[>s] ";
			break;
		case llvm::CmpInst::ICMP_SLE:
			APred = llvm::CmpInst::ICMP_SLT;
			Prefix = "[<s] ";
			break;
		case llvm::CmpInst::ICMP_UGE:
			APred = llvm::CmpInst::ICMP_UGT;
			Prefix = "[>u] ";
			break;
		case llvm::CmpInst::ICMP_ULE:
			APred = llvm::CmpInst::ICMP_ULT;
			Prefix = "[<u] ";
			break;
		}
		Msg = computeCmp(APred, LHS, RHS);
	}

	if (!Msg)
		return;

	if (report(llvm::Twine(Prefix) + Msg, I))
		OS << "    lhs -> " << *LHS << '\n'
		   << "    rhs -> " << *RHS << '\n';
}

void TautologicalCompare::warnOverflowCheck(llvm::ICmpInst *I) {
	llvm::Value *L = I->getOperand(0), *R = I->getOperand(1);
	const char *Msg = warnOverflowCheck(I->getPredicate(), L, R);
	if (!Msg && !I->isEquality())
		Msg = warnOverflowCheck(I->getSwappedPredicate(), R, L);
	if (!Msg)
		return;
	report(llvm::Twine(Msg) + " overflow check", I);
}

const char *TautologicalCompare::warnOverflowCheck(llvm::CmpInst::Predicate Pred, llvm::Value *L, llvm::Value *R) {
	llvm::Value *X, *Y;
	// x * y <[=] x
	if ((Pred == llvm::CmpInst::ICMP_ULT || Pred == llvm::CmpInst::ICMP_ULE)
	    && match(L, m_Mul(m_Value(X), m_Value(Y)))
	    && ((R == X) || (R == Y))) {
		// Skip x * 2 <[=] x.
		const llvm::SCEV *S = SE->getSCEV((R == X)? Y: X);
		const llvm::SCEV *Hi = SE->getConstant(S->getType(), 2);
		if (!SE->isKnownPredicate(llvm::CmpInst::ICMP_ULE, S, Hi))
			return "[mul]";
	}
	return 0;
}

// cmp ug[et] v, c
static bool isUG(llvm::Value *Cond, llvm::Value *V) {
	if (llvm::ICmpInst *I = llvm::dyn_cast<llvm::ICmpInst>(Cond)) {
		llvm::CmpInst::Predicate Pred = I->getPredicate();
		if (V == I->getOperand(0)
		    && (Pred == llvm::CmpInst::ICMP_UGT
		     || Pred == llvm::CmpInst::ICMP_UGE))
			return true;
		if (V == I->getOperand(1)
		    && (Pred == llvm::CmpInst::ICMP_ULT
		     || Pred == llvm::CmpInst::ICMP_ULE))
			return true;
	}
	return false;
}

// cond = cmp ug[et] v, c
// br cond ...
static bool isBranchUG(llvm::BasicBlock *BB, llvm::Value *V) {
	llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(BB->getTerminator());
	if (BI && BI->isConditional()) {
		if (isUG(BI->getCondition(), V))
			return true;
	}
	return false;
}

bool TautologicalCompare::isKnownCmp(llvm::ICmpInst *I) {
	llvm::CmpInst::Predicate Pred = I->getPredicate();
	llvm::Value *L = I->getOperand(0), *R = I->getOperand(1);
	// x <[=] 0 || x >[=] c
	if (Pred == llvm::CmpInst::ICMP_ULE || Pred == llvm::CmpInst::ICMP_ULT) {
		llvm::ConstantInt *CI = llvm::dyn_cast<llvm::ConstantInt>(R);
		// Okay it is x <[=] 0.
		if (CI && CI->isZero()) {
			if (!I->use_empty()) {
				llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(*I->use_begin());
				if (BO && BO->getOpcode() == llvm::Instruction::Or) {
					llvm::Value *Cond;
					if (BO->getOperand(0) == I) {
						Cond = BO->getOperand(1);
					} else {
						assert(I == BO->getOperand(1));
						Cond = BO->getOperand(0);
					}
					if (isUG(Cond, L))
						return true;
				}
			}
			llvm::BasicBlock *BB = I->getParent();
			// Check if the predecessor does x >[=] c.
			if (llvm::BasicBlock *Pred = BB->getSinglePredecessor()) {
				if (isBranchUG(Pred, L))
					return true;
			}
			// Check if the sucessor does x >[=] c.
			llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(BB->getTerminator());
			if (BI && BI->isConditional() && BI->getCondition() == I) {
				// The false branch.
				llvm::BasicBlock *Succ = BI->getSuccessor(1);
				if (isBranchUG(Succ, L))
					return true;
			}
		}
	}
	return false;
}

char TautologicalCompare::ID;

static llvm::RegisterPass<TautologicalCompare>
X("tautological-compare", "Warn tautological comparisons", false, true);
