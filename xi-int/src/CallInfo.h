#pragma once

#include <llvm/Module.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include "Linker.h"


class CallInfo {
public:
	CallInfo(LinkerContext *Ctx) : LinkerCtx(Ctx) { }

	// Give a function, return all possible callers (call sites)
	CSList& operator [] (llvm::Function *);
	
	// Give a call site, return all possible callees (functions)
	FuncSet& operator [] (llvm::CallInst *);
	
	void collectStructs(llvm::Module *M);
	void collectGlobalFunctions(llvm::Module *);
	void collectFPInitializers(llvm::Module *);
	bool collectFPAssignments(llvm::Module *);
	void collectCallers(llvm::Module *);
	
protected:
	LinkerContext *LinkerCtx;

	bool collectFPAssignments(llvm::Function *);
	bool collectFPAssignments(llvm::Value *, FuncSet&);
	bool collectFPAssignments(llvm::Value *, FuncSet&, 
							  llvm::SmallPtrSet<llvm::Value *, 4> &);
	void collectFPInitializers(llvm::Constant *);

private:
	bool isFunctionPointer(llvm::Type *);
	bool unionFuncPtrs(FuncSet &, const FuncSet &);
	bool unionFuncPtrs(FuncSet &, llvm::StringRef);
	
};