#define DEBUG_TYPE "lower-overflow"
#include <llvm/Instructions.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Pass.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Local.h>
#include <iostream>

using std::cerr;

namespace {

/// This passes replaces
///      x = llvm.op.with.overflow.* a, b
///      y = extract x, 0
/// with
///      y = op a, b
/// to make ScalarEvolution work well.  Note that the overflow bit
/// (extract x, 1) is still preserved.
struct LowerOverflow : llvm::FunctionPass {
	static char ID;
	LowerOverflow() : llvm::FunctionPass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
	}

	virtual bool runOnFunction(llvm::Function &);

private:
	bool visitExtractValueInst(llvm::ExtractValueInst *);
};

} // anonymous namespace

bool LowerOverflow::runOnFunction(llvm::Function &F) {
	cerr << "LowerOverflow::runOnFunction"  <<  "\n";
	bool Changed = false;
	llvm::inst_iterator i = inst_begin(F), e = inst_end(F);
	for (; i != e; ) {
		llvm::Instruction *I = &*i;
		++i;
		if (llvm::ExtractValueInst *EVI = llvm::dyn_cast<llvm::ExtractValueInst>(I))
			Changed |= visitExtractValueInst(EVI);
	}
	return Changed;
}

bool LowerOverflow::visitExtractValueInst(llvm::ExtractValueInst *I) {
	if (I->getNumIndices() != 1 || I->getIndices()[0] != 0)
		return false;
	llvm::IntrinsicInst *II = llvm::dyn_cast<llvm::IntrinsicInst>(I->getAggregateOperand());
	if (!II || II->getCalledFunction()->getName().find(".with.overflow.")
			== llvm::StringRef::npos)
		return false;
	llvm::Instruction::BinaryOps Op;
	switch (II->getIntrinsicID()) {
	default: II->dump(); assert(0 && "Unknown overflow!");
	case llvm::Intrinsic::sadd_with_overflow:
	case llvm::Intrinsic::uadd_with_overflow:
		Op = llvm::Instruction::Add;
		break;
	case llvm::Intrinsic::ssub_with_overflow:
	case llvm::Intrinsic::usub_with_overflow:
		Op = llvm::Instruction::Sub;
		break;
	case llvm::Intrinsic::smul_with_overflow:
	case llvm::Intrinsic::umul_with_overflow:
		Op = llvm::Instruction::Mul;
		break;
	}
	llvm::BinaryOperator *BO = llvm::BinaryOperator::Create(Op,
		II->getArgOperand(0), II->getArgOperand(1), "", I);
	I->replaceAllUsesWith(BO);
	llvm::RecursivelyDeleteTriviallyDeadInstructions(I);
	return true;
}

char LowerOverflow::ID;

static llvm::RegisterPass<LowerOverflow>
X("lower-overflow", "Lower overflow intrinsi");

llvm::Pass *createLowerOverflowPass() {
	return new LowerOverflow;
}
