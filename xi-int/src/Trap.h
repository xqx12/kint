#pragma once

#include <llvm/Function.h>
#include <llvm/Instructions.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Transforms/Utils/Local.h>

#define TRAP_SEPARATOR       "."
#define TRAP_PREFIX          "trap" TRAP_SEPARATOR
#define TRAP_ANNOTATE_PREFIX "__trap_annotate_i"
#define TRAP_ARG_ANNOTATIONS "llvm.arg.annotations"

namespace llvm {
	class Instruction;
	class Pass;
} // namespace llvm

llvm::Pass *createTrapAliasAnalysisPass();
llvm::Pass *createTrapCombinePass();
llvm::Pass *createTrapGenPass();
llvm::Pass *createTrapLinuxPass();
llvm::Pass *createTrapLoopPass();
llvm::Pass *createTrapSimplifyPass();

void generateTrap(llvm::Instruction *);

void insertTrapCall(
	const llvm::Twine &FName,
	llvm::ArrayRef<llvm::Value *> Args,
	llvm::Instruction *InsertPoint
);

namespace Trap {

inline static
bool isa(const llvm::Instruction *I) {
	if (const llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(I))
		if (llvm::Function *F = CI->getCalledFunction())
			return F->getName().startswith(TRAP_PREFIX);
	return false;
}

inline static
llvm::StringRef getName(const llvm::Instruction *I) {
	if (const llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(I)) {
		if (llvm::Function *F = CI->getCalledFunction()) {
			llvm::StringRef Name = F->getName();
			if (Name.startswith(TRAP_PREFIX))
				return Name.substr(Name.find(TRAP_SEPARATOR) + 1);
		}
	}
	return "";
}

inline static
bool isCommutative(llvm::StringRef Name) {
	return Name.startswith("sadd") || Name.startswith("uadd")
	|| Name.startswith("smul") || Name.startswith("umul");
}

inline static
bool RecursivelyDeleteTriviallyDeadInstructions(llvm::Instruction *I) {
	// Temporarily set trap call to readnone for deletion.
	if (isa(I)) {
		assert(I->use_empty());
		llvm::cast<llvm::CallInst>(I)->setDoesNotAccessMemory();
	}
	return llvm::RecursivelyDeleteTriviallyDeadInstructions(I);
}

} // namespace Trap
