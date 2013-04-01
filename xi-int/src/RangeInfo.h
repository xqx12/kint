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


class RangeInfo {
public:
	RangeInfo(LinkerContext *Ctx) : MaxIterations(5), LinkerCtx(Ctx) { }
	
	void collectInitializers(llvm::Module *);

	bool updateRangeFor(llvm::Module *M);
	
protected:
	const unsigned MaxIterations;	
	LinkerContext *LinkerCtx;
	
	bool safeUnion(llvm::ConstantRange &CR, const llvm::ConstantRange &R);
	bool unionRange(llvm::StringRef, const llvm::ConstantRange &, llvm::Value *);
	bool unionRange(llvm::BasicBlock *, llvm::Value *, const llvm::ConstantRange &);
	llvm::ConstantRange getRange(llvm::BasicBlock *, llvm::Value *);

	void collectInitializers(llvm::GlobalVariable *, llvm::Constant *);
	bool updateRangeFor(llvm::Function *);
	bool updateRangeFor(llvm::BasicBlock *);
	bool updateRangeFor(llvm::Instruction *);

private:

	typedef std::map<llvm::Value *, llvm::ConstantRange> ValueRangeMap;
	typedef std::map<llvm::BasicBlock *, ValueRangeMap> FuncValueRangeMaps;
	FuncValueRangeMaps FuncVRMs;

	typedef std::set<std::string> ChangeSet;
	ChangeSet Changes;
	
	typedef std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *> Edge;
	typedef llvm::SmallVector<Edge, 16> EdgeList;
	EdgeList BackEdges;
	
	bool isBackEdge(const Edge &);
	
	llvm::ConstantRange visitBinaryOp(llvm::BinaryOperator *);
	llvm::ConstantRange visitCastInst(llvm::CastInst *);
	llvm::ConstantRange visitSelectInst(llvm::SelectInst *);
	llvm::ConstantRange visitPHINode(llvm::PHINode *);
	
	bool visitCallInst(llvm::CallInst *);
	bool visitReturnInst(llvm::ReturnInst *);
	bool visitStoreInst(llvm::StoreInst *);

	void visitBranchInst(llvm::BranchInst *, 
						 llvm::BasicBlock *, ValueRangeMap &);
	void visitTerminator(llvm::TerminatorInst *,
						 llvm::BasicBlock *, ValueRangeMap &);
	void visitSwitchInst(llvm::SwitchInst *, 
						 llvm::BasicBlock *, ValueRangeMap &);
};
