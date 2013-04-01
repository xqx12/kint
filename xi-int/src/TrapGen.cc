#define DEBUG_TYPE "trap-gen"
#include "Trap.h"
#include <llvm/Constants.h>
#include <llvm/Instructions.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Transforms/Utils/Local.h>
#include <iostream>

using std::cerr;

STATISTIC(NumTrapCalls, "Number of trap calls inserted");

void insertTrapCall(
	const llvm::Twine &Name,
	llvm::ArrayRef<llvm::Value *> Args,
	llvm::Instruction *InsertPoint
) {
	DEBUG(llvm::dbgs() << "Insert " << Name << "\n");
	//cerr << "insertTrapCall Insert "  << "\n" ;
	llvm::dbgs() << "insertTrapCall Insert " << Name << "\n";
	std::vector<llvm::Type *> ArgTys;
	for (size_t i = 0, n = Args.size(); i != n; ++i)
		ArgTys.push_back(Args[i]->getType());
	assert(!ArgTys.empty());
	llvm::Type *VoidTy = llvm::Type::getVoidTy(InsertPoint->getContext());
	llvm::FunctionType *FT = llvm::FunctionType::get(
		VoidTy, ArgTys, false
	);
	llvm::Module *M = InsertPoint->getParent()->getParent()->getParent();
	llvm::Constant *C = M->getOrInsertFunction(Name.str(), FT);
	llvm::Function *F = llvm::cast<llvm::Function>(C);
	// Set trap function attributes.
	F->setDoesNotThrow();
	// Create a call instruction.
	llvm::Instruction *I = llvm::CallInst::Create(F, Args, "", InsertPoint);
	// Copy !dbg metadata.
	I->setDebugLoc(InsertPoint->getDebugLoc());
	++NumTrapCalls;
}

// for trunc/index/size, based on annotations.
static void generateUnaryTrap(llvm::Instruction *I) {
	llvm::CallSite CS(I);
	if (!CS)
		return;
	llvm::Function *F = CS.getCalledFunction();
	if (!F || !F->getName().startswith(TRAP_ANNOTATE_PREFIX))
		return;
	// __trap_annotation_*(val, annotation)
	assert(CS.arg_size() == 2);
	llvm::Value *V = CS.getArgument(0);
	assert(V->getType() == CS.getType());
	llvm::StringRef Anno;
	if (!llvm::getConstantStringInfo(CS.getArgument(1), Anno)) {
		I->dump();
		llvm::report_fatal_error("Bad annotation!");
	}
	// Insert trap call.
	insertTrapCall(TRAP_PREFIX + Anno, V, I);
	// Make the annotation call obsolete.
	I->replaceAllUsesWith(V);
	llvm::RecursivelyDeleteTriviallyDeadInstructions(I);
}

static void generateBinaryTrap(llvm::Instruction *I) {
	llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(I);
	if (!BO)
		return;
	llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(I->getType());
	if (!T)
		return;
	const char *Op, *OpPrefix = "";
	std::vector<llvm::Value *> Args;
	switch (BO->getOpcode()) {
	default: return;
	case llvm::Instruction::Add:
	case llvm::Instruction::Sub:
	case llvm::Instruction::Mul:
		if (BO->getMetadata("ovf"))
			return;
		OpPrefix = BO->hasNoSignedWrap()? "s": "u";
		// Fall through.
	case llvm::Instruction::SDiv:
	case llvm::Instruction::SRem:
		Op = BO->getOpcodeName();
		Args.push_back(BO->getOperand(0));
		Args.push_back(BO->getOperand(1));
		break;
	case llvm::Instruction::UDiv:
	case llvm::Instruction::URem:
	case llvm::Instruction::Shl:
	case llvm::Instruction::LShr:
	case llvm::Instruction::AShr:
		Op = BO->getOpcodeName();
		Args.push_back(BO->getOperand(1));
		break;
	}
	const llvm::Twine &Name = TRAP_PREFIX
		+ llvm::Twine(OpPrefix) + Op + TRAP_SEPARATOR
		+ "i" + llvm::Twine(T->getBitWidth());
	insertTrapCall(Name, Args, BO);
}

struct TrapGen : llvm::ModulePass {
	static char ID;
	TrapGen() : llvm::ModulePass(ID) { }

	virtual bool runOnModule(llvm::Module &M) {
		cerr << "TrapGen::runOnModule called\n" ;
		for (llvm::Module::iterator mi = M.begin(),
		     me = M.end(); mi != me; ++mi) {
			llvm::Function *F = mi;
			if (F->empty())
				continue;
			// Generate index/size & remove annotations first.
			// Otherwise hasAddSubUWithNegCheck may not work.
			for (llvm::inst_iterator i = inst_begin(F),
			     e = inst_end(F); i != e; ) {
				llvm::Instruction *I = &*i++;
				generateUnaryTrap(I);
			}
			for (llvm::inst_iterator i = inst_begin(F),
			     e = inst_end(F); i != e; ) {
				llvm::Instruction *I = &*i++;
				generateBinaryTrap(I);
			}
		}
		return true;
	}
};

char TrapGen::ID;

static llvm::RegisterPass<TrapGen>
X("trap-gen", "Generate trap calls");

llvm::Pass *createTrapGenPass() {
	return new TrapGen;
}

void generateTrap(llvm::Instruction *I) {
	generateBinaryTrap(I);
}
