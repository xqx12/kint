#pragma once

#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/Constants.h>
#include <llvm/Instructions.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Analysis/DebugInfo.h>
#include <llvm/Analysis/CallGraph.h>
#include "Linker.h"

class TaintInfo {
public:
	TaintInfo(LinkerContext *Ctx) : LinkerCtx(Ctx) { }
	
	bool updateTaint(llvm::Module *M);
	bool isTaintSource(const std::string &sID);
	
protected:
	LinkerContext *LinkerCtx;
	typedef llvm::SmallPtrSet<llvm::Value *, 16> ValueTaintSet;
	ValueTaintSet VTS;
	
	bool updateTaint(llvm::Function *F);
	bool checkTaintSource(llvm::Instruction *);
	bool checkTaintSource(llvm::Function *);

	
private:
	bool isTaint(llvm::Value *);
	bool markTaint(const std::string &, bool);

};
