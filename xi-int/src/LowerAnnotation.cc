#define DEBUG_TYPE "lower-annotation"
#include <llvm/Instructions.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Metadata.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/Support/InstIterator.h>
#include <iostream>

using namespace std;
using std::cerr;

namespace {

struct LowerAnnotation : llvm::FunctionPass {
	static char ID;
	LowerAnnotation() : llvm::FunctionPass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
	}

	virtual bool runOnFunction(llvm::Function &F);
};

} // anonymous namespace

static llvm::IntrinsicInst *getAsPtrAnnotation(llvm::Value *V) {
	llvm::IntrinsicInst *I = llvm::dyn_cast<llvm::IntrinsicInst>(
		V->stripPointerCasts()
	);
	if (I && I->getIntrinsicID() == llvm::Intrinsic::ptr_annotation)
		return I;
	return 0;
}

bool LowerAnnotation::runOnFunction(llvm::Function &F) {
	std::cerr << "LowerAnnotation::runOnFunction"  <<  "\n";
	llvm::LLVMContext &VMCtx = F.getContext();
	for (llvm::inst_iterator i = inst_begin(F),
	     e = inst_end(F); i != e; ++i) {
		llvm::Instruction *I = &*i;
		unsigned Idx;
		if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(I))
			Idx = LI->getPointerOperandIndex();
		else if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(I))
			Idx = SI->getPointerOperandIndex();
		else
			continue;
		// Pointer operand is the return value of llvm.ptr.annotation.*.
		llvm::Value *V = I->getOperand(Idx);
		llvm::IntrinsicInst *II = getAsPtrAnnotation(V);
		if (!II)
			continue;
		assert(II->getNumArgOperands() == 4);
		llvm::StringRef Anno;
		if (!llvm::getConstantStringInfo(II->getOperand(1), Anno)) {
			II->dump();
			llvm::report_fatal_error("Bad annotation!");
		}
		// Annotation should be in the form of key:value.
		std::string::size_type Pos = Anno.find(':');
		if (Pos == std::string::npos)
			continue;
		// Set metadata.
		llvm::MDNode *MD = llvm::MDNode::get(VMCtx,
			llvm::MDString::get(VMCtx, Anno.substr(Pos + 1)));
		I->setMetadata(Anno.substr(0, Pos), MD);
	}
	// Remove llvm.ptr.annotation.* calls.
	bool Changed = false;
	for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ) {
		llvm::IntrinsicInst *I = getAsPtrAnnotation(&*i);
		++i;
		if (!I)
			continue;
		llvm::Value *V = I->getArgOperand(0);
		assert(I->getType() == V->getType());
		I->replaceAllUsesWith(V);
		I->eraseFromParent();
		Changed = true;
	}
	return Changed;
}

char LowerAnnotation::ID;

static llvm::RegisterPass<LowerAnnotation>
X("lower-annotation", "Lower llvm.ptr.annotation.* calls to metadata");

llvm::Pass *createLowerAnnotationPass() {
	return new LowerAnnotation;
}
