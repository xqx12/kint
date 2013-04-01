#define DEBUG_TYPE "ranges"
#include <llvm/Pass.h>
#include <llvm/Instructions.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Module.h>
#include <llvm/Constants.h>
#include <llvm/ADT/OwningPtr.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Analysis/DebugInfo.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include "llvm/Support/CommandLine.h"


#include "Linker.h"
#include "RangeInfo.h"
#include "TaintInfo.h"

#ifndef __APPLE__
static llvm::cl::opt<std::string>
WatchID("w", llvm::cl::desc("Watch sID"), 
			   llvm::cl::value_desc("sID"));
#else
static std::string WatchID = "";
#endif

void conv_and_warn_if_unmatch(llvm::ConstantRange &V1, llvm::ConstantRange &V2)
{
	if (V1.getBitWidth() != V2.getBitWidth()) {
		llvm::dbgs() << "warning: range " << V1 << " " << V1.getBitWidth()
			<< " and " << V2 << " " << V2.getBitWidth() << " unmatch\n";
		V2 = V2.zextOrTrunc(V1.getBitWidth());
	}
}

bool RangeInfo::safeUnion(llvm::ConstantRange &CR, const llvm::ConstantRange &R)
{
	llvm::ConstantRange V = R, Old = CR;
	conv_and_warn_if_unmatch(CR, V);
	CR = CR.unionWith(V);
	return Old != CR;
}

bool RangeInfo::unionRange(llvm::StringRef sID, const llvm::ConstantRange &R,
						   llvm::Value *V = NULL)
{
	if (R.isEmptySet())
		return false;
	
	if (WatchID == sID && V) {
		if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V))
			llvm::dbgs() << I->getParent()->getParent()->getName() << "(): ";
		V->print(llvm::dbgs());
		llvm::dbgs() << "\n";
	}
	
	bool changed = true;
	IntRangeMap::iterator it = LinkerCtx->IntRanges.find(sID);
	if (it != LinkerCtx->IntRanges.end()) {
		changed = safeUnion(it->second, R);
		if (changed && sID == WatchID)
			llvm::dbgs() << sID << " + " << R << " = " << it->second << "\n";
	} else {
		LinkerCtx->IntRanges.insert(std::make_pair(sID, R));
		if (sID == WatchID)
			llvm::dbgs() << sID << " = " << R << "\n";
	}
	if (changed)
		Changes.insert(sID);
	return changed;
}

bool RangeInfo::unionRange(llvm::BasicBlock *BB, llvm::Value *V,
						   const llvm::ConstantRange &R)
{
	if (R.isEmptySet())
		return false;
	
	bool changed = true;
	ValueRangeMap &VRM = FuncVRMs[BB];
	ValueRangeMap::iterator it = VRM.find(V);
	if (it != VRM.end())
		changed = safeUnion(it->second, R);
	else
		VRM.insert(std::make_pair(V, R));
	return changed;
}

llvm::ConstantRange RangeInfo::getRange(llvm::BasicBlock *BB, llvm::Value *V)
{
	// constants
	if (llvm::ConstantInt *C = llvm::dyn_cast<llvm::ConstantInt>(V))
		return llvm::ConstantRange(C->getValue());
	
	ValueRangeMap &VRM = FuncVRMs[BB];
	ValueRangeMap::iterator invrm = VRM.find(V);
	
	if (invrm != VRM.end())
		return invrm->second;
	
	// V must be integer or pointer to integer
	llvm::IntegerType *Ty = llvm::dyn_cast<llvm::IntegerType>(V->getType());
	if (llvm::PointerType *PTy = llvm::dyn_cast<llvm::PointerType>(V->getType()))
		Ty = llvm::dyn_cast<llvm::IntegerType>(PTy->getElementType());
	assert(Ty != NULL);
	
	// not found in VRM, lookup global range, return empty set by default
	llvm::ConstantRange CR(Ty->getBitWidth(), false);
	llvm::ConstantRange Fullset(Ty->getBitWidth(), true);
	
	IntRangeMap &IRM = LinkerCtx->IntRanges;
	TaintInfo TI(LinkerCtx);
	
	if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(V)) {
		// calculate union of values ranges returned by all possible callees
		if (!CI->isInlineAsm() && LinkerCtx->Callees.count(CI)) {
			FuncSet &CEEs = LinkerCtx->Callees[CI];
			for (FuncSet::iterator i = CEEs.begin(), e = CEEs.end();
				 i != e; ++i) {
				std::string sID = getRetID(*i);
				if (sID != "" && TI.isTaintSource(sID)) {
					CR = Fullset;
					break;
				}
				IntRangeMap::iterator it;
				if ((it = IRM.find(sID)) != IRM.end())
					safeUnion(CR, it->second);
			}
		}
	} else {
		// arguments & loads
		std::string sID = getValueID(V);
		if (sID != "") {
			IntRangeMap::iterator it;
			if (TI.isTaintSource(sID))
				CR = Fullset;
			else if ((it = IRM.find(sID)) != IRM.end())
				CR = it->second;
		}
		// might load part of a struct field
		CR = CR.zextOrTrunc(Ty->getBitWidth());
	}
	if (!CR.isEmptySet())
		VRM.insert(std::make_pair(V, CR));
	return CR;
}

void RangeInfo::collectInitializers(llvm::GlobalVariable *GV, llvm::Constant *I)
{	
	// global var
	if (llvm::ConstantInt *CI = llvm::dyn_cast<llvm::ConstantInt>(I)) {
		unionRange(getVarID(GV), CI->getValue(), GV);
	}
	
	// structs
	if (llvm::ConstantStruct *CS = llvm::dyn_cast<llvm::ConstantStruct>(I)) {
		// Find integer fields in the struct
		llvm::StructType *ST = CS->getType();
		// Skip anonymous structs
		if (!ST->hasName() || ST->getName() == "struct.anon" 
			|| ST->getName().startswith("struct.anon."))
			return;
		
		for (unsigned i = 0; i != ST->getNumElements(); ++i) {
			llvm::Type *Ty = ST->getElementType(i);
			if (Ty->isStructTy()) {
				// nested struct
				// TODO: handle nested arrays
				collectInitializers(GV, CS->getOperand(i));
			} else if (Ty->isIntegerTy()) {
				llvm::ConstantInt *CI = 
					llvm::dyn_cast<llvm::ConstantInt>(I->getOperand(i));
				llvm::StringRef sID = LinkerCtx->getFieldID(ST, i);
				if (!sID.empty() && CI)
					unionRange(sID, CI->getValue(), GV);
			}
		}
	}

	// arrays
	if (llvm::ConstantArray *CA = llvm::dyn_cast<llvm::ConstantArray>(I)) {
		llvm::Type *Ty = CA->getType()->getElementType();
		if (Ty->isStructTy() || Ty->isIntegerTy()) {
			for (unsigned i = 0; i != CA->getNumOperands(); ++i)
				collectInitializers(GV, CA->getOperand(i));
		}
	}
}

//
// Handle integer assignments in global initializers
//
void RangeInfo::collectInitializers(llvm::Module *M)
{	
	// Looking for global variables
	for (llvm::Module::global_iterator i = M->global_begin(), 
		 e = M->global_end(); i != e; ++i) {

		// skip strings literals
		if (i->hasInitializer() && !i->getName().startswith("."))
			collectInitializers(&*i, i->getInitializer());
	}
}


llvm::ConstantRange RangeInfo::visitBinaryOp(llvm::BinaryOperator *BO)
{
	llvm::ConstantRange L = getRange(BO->getParent(), BO->getOperand(0));
	llvm::ConstantRange R = getRange(BO->getParent(), BO->getOperand(1));
	conv_and_warn_if_unmatch(L, R);
	switch (BO->getOpcode()) {
		default: BO->dump(); llvm_unreachable("Unknown binary operator!");
		case llvm::Instruction::Add:  return L.add(R);
		case llvm::Instruction::Sub:  return L.sub(R);
		case llvm::Instruction::Mul:  return L.multiply(R);
		case llvm::Instruction::UDiv: return L.udiv(R);
		case llvm::Instruction::SDiv: return L; // FIXME
		case llvm::Instruction::URem: return R; // FIXME
		case llvm::Instruction::SRem: return R; // FIXME
		case llvm::Instruction::Shl:  return L.shl(R);
		case llvm::Instruction::LShr: return L.lshr(R);
		case llvm::Instruction::AShr: return L; // FIXME
		case llvm::Instruction::And:  return L.binaryAnd(R);
		case llvm::Instruction::Or:   return L.binaryOr(R);
		case llvm::Instruction::Xor:  return L; // FIXME
	}
}


llvm::ConstantRange RangeInfo::visitCastInst(llvm::CastInst *CI)
{	
	unsigned bits = llvm::dyn_cast<llvm::IntegerType>(
								CI->getDestTy())->getBitWidth();
	
	// pointer to int could be any value
	if (CI->getOpcode() == llvm::CastInst::PtrToInt)
		return llvm::ConstantRange(bits, true);
	
	llvm::ConstantRange CR = getRange(CI->getParent(), CI->getOperand(0));
	switch (CI->getOpcode()) {
		default: CI->dump(); llvm_unreachable("unknown cast inst");
		case llvm::CastInst::Trunc:    return CR.zextOrTrunc(bits);
		case llvm::CastInst::ZExt:     return CR.zextOrTrunc(bits);
		case llvm::CastInst::SExt:     return CR.signExtend(bits);
		case llvm::CastInst::BitCast:  return CR;
	}
}

llvm::ConstantRange RangeInfo::visitSelectInst(llvm::SelectInst *SI)
{
	llvm::ConstantRange T = getRange(SI->getParent(), SI->getTrueValue());
	llvm::ConstantRange F = getRange(SI->getParent(), SI->getFalseValue());
	safeUnion(T, F);
	return T;
}

llvm::ConstantRange RangeInfo::visitPHINode(llvm::PHINode *PHI)
{
	llvm::IntegerType *Ty = llvm::dyn_cast<llvm::IntegerType>(PHI->getType());
	assert(Ty);
	llvm::ConstantRange CR(Ty->getBitWidth(), false);
	
	for (unsigned i = 0, n = PHI->getNumIncomingValues(); i < n; ++i) {
		llvm::BasicBlock *Pred = PHI->getIncomingBlock(i);
		// skip back edges
		if (isBackEdge(Edge(Pred, PHI->getParent())))
			continue;
		safeUnion(CR, getRange(Pred, PHI->getIncomingValue(i)));
	}
	return CR;
}

bool RangeInfo::visitCallInst(llvm::CallInst *CI)
{
	bool changed = false;
	if (CI->isInlineAsm() || LinkerCtx->Callees.count(CI) == 0)
		return false;

	// update arguments of all possible callees
	FuncSet &CEEs = LinkerCtx->Callees[CI];
	for (FuncSet::iterator i = CEEs.begin(), e = CEEs.end(); i != e; ++i) {
		// skip vaarg and builtin functions
		if ((*i)->isVarArg() 
			|| (*i)->getName().find('.') != llvm::StringRef::npos)
			continue;
		
		for (unsigned j = 0; j < CI->getNumArgOperands(); ++j) {
			llvm::Value *V = CI->getArgOperand(j);
			// skip non-integer arguments
			if (!V->getType()->isIntegerTy())
				continue;
			std::string sID = getArgID(*i, j);
			changed |= unionRange(sID, getRange(CI->getParent(), V), CI);
		}
	}
	// range for the return value of this call site
	if (CI->getType()->isIntegerTy())
		changed |= unionRange(getRetID(CI), getRange(CI->getParent(), CI), CI);
	return changed;
}

bool RangeInfo::visitStoreInst(llvm::StoreInst *SI)
{
	std::string sID = getValueID(SI);
	llvm::Value *V = SI->getValueOperand();
	if (V->getType()->isIntegerTy() && sID != "") {
		llvm::ConstantRange CR = getRange(SI->getParent(), V);
		unionRange(SI->getParent(), SI->getPointerOperand(), CR);
		return unionRange(sID, CR, SI);
	}
	return false;
}

bool RangeInfo::visitReturnInst(llvm::ReturnInst *RI)
{
	llvm::Value *V = RI->getReturnValue();
	if (!V || !V->getType()->isIntegerTy())
		return false;
	
	std::string sID = getRetID(RI->getParent()->getParent());
	return unionRange(sID, getRange(RI->getParent(), V), RI);
}

bool RangeInfo::updateRangeFor(llvm::Instruction *I)
{
	bool changed = false;
	
	// store, return and call might update global range
	if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(I)) {
		changed |= visitStoreInst(SI);
	} else if (llvm::ReturnInst *RI = llvm::dyn_cast<llvm::ReturnInst>(I)) {
		changed |= visitReturnInst(RI);
	} else if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(I)) {
		changed |= visitCallInst(CI);
	}
	
	llvm::IntegerType *Ty = llvm::dyn_cast<llvm::IntegerType>(I->getType());
	if (!Ty)
		return changed;
	
	llvm::ConstantRange CR(Ty->getBitWidth(), true);
	if (llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(I)) {
		CR = visitBinaryOp(BO);
	} else if (llvm::CastInst *CI = llvm::dyn_cast<llvm::CastInst>(I)) {
		CR = visitCastInst(CI);
	} else if (llvm::SelectInst *SI = llvm::dyn_cast<llvm::SelectInst>(I)) {
		CR = visitSelectInst(SI);
	} else if (llvm::PHINode *PHI = llvm::dyn_cast<llvm::PHINode>(I)) {
		CR = visitPHINode(PHI);
	} else if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
		CR = getRange(LI->getParent(), LI);
	} else if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(I)) {
		CR = getRange(CI->getParent(), CI);
	}
	unionRange(I->getParent(), I, CR);
	
	return changed;
}

bool RangeInfo::isBackEdge(const Edge &E)
{
	return std::find(BackEdges.begin(), BackEdges.end(), E)	!= BackEdges.end();
}

void RangeInfo::visitBranchInst(llvm::BranchInst *BI, llvm::BasicBlock *BB, 
								ValueRangeMap &VRM)
{
	if (!BI->isConditional())
		return;
	
	llvm::ICmpInst *ICI = llvm::dyn_cast<llvm::ICmpInst>(BI->getCondition());
	if (ICI == NULL)
		return;
	
	llvm::Value *LHS = ICI->getOperand(0);
	llvm::Value *RHS = ICI->getOperand(1);
	
	if (!LHS->getType()->isIntegerTy() || !RHS->getType()->isIntegerTy())
		return;
	
	llvm::ConstantRange LCR = getRange(ICI->getParent(), LHS);
	llvm::ConstantRange RCR = getRange(ICI->getParent(), RHS);
	conv_and_warn_if_unmatch(LCR, RCR);

	if (BI->getSuccessor(0) == BB) {
		// true target
		llvm::ConstantRange PLCR = llvm::ConstantRange::makeICmpRegion(
									ICI->getSwappedPredicate(), LCR);
		llvm::ConstantRange PRCR = llvm::ConstantRange::makeICmpRegion(
									ICI->getPredicate(), RCR);
		VRM.insert(std::make_pair(LHS, LCR.intersectWith(PRCR)));
		VRM.insert(std::make_pair(RHS, LCR.intersectWith(PLCR)));
	} else {
		// false target, use inverse predicate
		// N.B. why there's no getSwappedInversePredicate()...
		ICI->swapOperands();
		llvm::ConstantRange PLCR = llvm::ConstantRange::makeICmpRegion(
									ICI->getInversePredicate(), RCR);
		ICI->swapOperands();
		llvm::ConstantRange PRCR = llvm::ConstantRange::makeICmpRegion(
									ICI->getInversePredicate(), RCR);
		VRM.insert(std::make_pair(LHS, LCR.intersectWith(PRCR)));
		VRM.insert(std::make_pair(RHS, LCR.intersectWith(PLCR)));
	}
}

void RangeInfo::visitSwitchInst(llvm::SwitchInst *SI, llvm::BasicBlock *BB, 
								ValueRangeMap &VRM)
{
	llvm::Value *V = SI->getCondition();
	llvm::IntegerType *Ty = llvm::dyn_cast<llvm::IntegerType>(V->getType());
	if (!Ty)
		return;
	
	llvm::ConstantRange VCR = getRange(SI->getParent(), V);
	llvm::ConstantRange CR(Ty->getBitWidth(), false);
						   
	if (SI->getDefaultDest() != BB) {
		// union all values that goes to BB
		for (llvm::SwitchInst::CaseIt i = SI->case_begin(), e = SI->case_end();
			 i != e; ++i) {
			if (i.getCaseSuccessor() == BB)
				safeUnion(CR, i.getCaseValue()->getValue());
		}
	} else {
		// default case
		for (llvm::SwitchInst::CaseIt i = SI->case_begin(), e = SI->case_end();
			 i != e; ++i)
			safeUnion(CR, i.getCaseValue()->getValue());
		CR = CR.inverse();
	}
	VRM.insert(std::make_pair(V, VCR.intersectWith(CR)));
}

void RangeInfo::visitTerminator(llvm::TerminatorInst *I, llvm::BasicBlock *BB,
								 ValueRangeMap &VRM) {
	if (llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(I))
		visitBranchInst(BI, BB, VRM);
	else if (llvm::SwitchInst *SI = llvm::dyn_cast<llvm::SwitchInst>(I))
		visitSwitchInst(SI, BB, VRM);
	else {
		I->dump(); llvm_unreachable("Unknown terminator!");
	}
}


bool RangeInfo::updateRangeFor(llvm::BasicBlock *BB)
{
	bool changed = false;

	// propagate value ranges from pred BBs, ranges in BB are union of ranges
	// in pred BBs, constrained by each terminator.
	for (llvm::pred_iterator i = llvm::pred_begin(BB), e = llvm::pred_end(BB);
			i != e; ++i) {
		llvm::BasicBlock *Pred = *i;
		if (isBackEdge(Edge(Pred, BB)))
			continue;
		
		ValueRangeMap &PredVRM = FuncVRMs[Pred];
		ValueRangeMap &BBVRM = FuncVRMs[BB];
		
		// Copy from its predecessor
		ValueRangeMap VRM(PredVRM.begin(), PredVRM.end());
		// Refine according to the terminator
		visitTerminator(Pred->getTerminator(), BB, VRM);
		
		// union with other predecessors
		for (ValueRangeMap::iterator j = VRM.begin(), je = VRM.end();
			 j != je; ++j) {
			ValueRangeMap::iterator it = BBVRM.find(j->first);
			if (it != BBVRM.end())
				safeUnion(it->second, j->second);
			else
				BBVRM.insert(*j);
		}
	}
	
	// Now run through instructions
	for (llvm::BasicBlock::iterator i = BB->begin(), e = BB->end(); 
		 i != e; ++i) {
		changed |= updateRangeFor(&*i);
	}
	
	return changed;
}

bool RangeInfo::updateRangeFor(llvm::Function *F)
{
	bool changed = false;
	
	FuncVRMs.clear();
	BackEdges.clear();
	llvm::FindFunctionBackedges(*F, BackEdges);
	
	for (llvm::Function::iterator b = F->begin(), be = F->end(); b != be; ++b)
		changed |= updateRangeFor(&*b);
	
	return changed;
}

bool RangeInfo::updateRangeFor(llvm::Module *M)
{
	unsigned itr = 0;
	bool changed = true, ret = false;

	while (changed) {
		// if some values converge too slowly, expand them to full-set
		if (++itr > MaxIterations) {
			for (ChangeSet::iterator it = Changes.begin(), ie = Changes.end();
				 it != ie; ++it) {
				IntRangeMap::iterator i = LinkerCtx->IntRanges.find(*it);
				i->second = llvm::ConstantRange(i->second.getBitWidth());
			}
		}
		changed = false;
		Changes.clear();
		for (llvm::Module::iterator i = M->begin(), e = M->end(); i != e; ++i)
			if (!i->empty())
				changed |= updateRangeFor(&*i);
		ret |= changed;
	}
	return ret;
}

