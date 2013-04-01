#define DEBUG_TYPE "callinfo"
#include <llvm/Pass.h>
#include <llvm/Instructions.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Module.h>
#include <llvm/Constants.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Analysis/DebugInfo.h>
#include <llvm/Analysis/CallGraph.h>

#include "CallInfo.h"
#include <iostream>

using std::cerr;


CSList& CallInfo::operator [] (llvm::Function *F) {
	return LinkerCtx->Callers[F];
}

FuncSet& CallInfo::operator [] (llvm::CallInst *CI) {
	return LinkerCtx->Callees[CI];
}

bool CallInfo::isFunctionPointer(llvm::Type *T)
{
	llvm::PointerType *Ty = llvm::dyn_cast<llvm::PointerType>(T);
	return Ty && Ty->getElementType()->isFunctionTy();
}

void CallInfo::collectGlobalFunctions(llvm::Module *M)
{
	for (llvm::Module::iterator i = M->begin(), e = M->end(); i != e; ++i) {
		if ((i->hasExternalLinkage() || i->hasExternalWeakLinkage())
			&& !i->empty())
			LinkerCtx->GlobalFuncs[i->getName().str()] = &(*i);
	}
}

void CallInfo::collectFPInitializers(llvm::Constant *I)
{
	llvm::dbgs() << "CallInfo::collectFPInitializers called by dbgs\n   ";
	cerr << "CallInfo::collectFPInitializers called \n" ;
	if (llvm::ConstantStruct *CS = llvm::dyn_cast<llvm::ConstantStruct>(I)) {
		// Find function pointer fields in the struct
		llvm::StructType *ST = CS->getType();
		if (!ST->hasName())
			return;
		
		for (unsigned i = 0; i != ST->getNumElements(); ++i) {
			llvm::Type *Ty = ST->getElementType(i);
			if (Ty->isStructTy() || Ty->isArrayTy()) {
				// nested array or struct
				collectFPInitializers(CS->getOperand(i));
			} else if (isFunctionPointer(Ty)) {
				llvm::Function *F = 
					llvm::dyn_cast<llvm::Function>(I->getOperand(i));
				llvm::StringRef sID = LinkerCtx->getFieldID(ST, i);
				if (!sID.empty() && F)
					LinkerCtx->FuncPtrs[sID].insert(F);
			}
		}
	}

	// array of structs
	if (llvm::ConstantArray *CA = llvm::dyn_cast<llvm::ConstantArray>(I)) {
		if (CA->getType()->getElementType()->isStructTy()) {
			for (unsigned i = 0; i != CA->getNumOperands(); ++i)
				collectFPInitializers(CA->getOperand(i));
		}
	}
}

//
// Handle function pointer assignments in global initializers
//
void CallInfo::collectFPInitializers(llvm::Module *M)
{	
	// Now looking for global initializers
	for (llvm::Module::global_iterator i = M->global_begin(), 
		 e = M->global_end(); i != e; ++i) {

		// skip non-struct or external global variables
		if (i->hasInitializer())
			collectFPInitializers(i->getInitializer());
	}
}

void CallInfo::collectStructs(llvm::Module *M)
{
	cerr << "CallInfo::collectStructs called \n" ;
	// Load field index ==> ID mappings from metadata
	llvm::NamedMDNode *NMD = M->getNamedMetadata("cint.structs");
	if (!NMD)
		return;
	for (unsigned i = 0; i != NMD->getNumOperands(); ++i) {
		llvm::MDNode *MD = NMD->getOperand(i);
		std::string RN = "struct." + llvm::dyn_cast<llvm::MDString>
			(MD->getOperand(0))->getString().str();
		for (unsigned j = 1; j != MD->getNumOperands(); ++j) {
			llvm::StringRef FN =
				llvm::dyn_cast<llvm::MDString>(MD->getOperand(j))->getString();
			std::string ID = RN + "." + FN.str();
			LinkerCtx->FID[RN].push_back(ID);
		}
		// put a barrier at the end
		LinkerCtx->FID[RN].push_back("______");
	}
}


//
// Propogate FP-set in FuncPtrs[sID] to FP-set S
//
bool CallInfo::unionFuncPtrs(FuncSet &S, llvm::StringRef sID)
{
	FuncPtrMap::iterator i = LinkerCtx->FuncPtrs.find(sID);
	if (i != LinkerCtx->FuncPtrs.end())
		return unionFuncPtrs(S, i->second);
	return false;
}

bool CallInfo::unionFuncPtrs(FuncSet &dst, const FuncSet &src)
{
	bool changed = false;
	for (FuncSet::const_iterator i = src.begin(), e = src.end(); i != e; ++i)
		changed |= dst.insert(*i);
	return changed;
}


//
// Insert R-value V to FP-set S
//
bool CallInfo::collectFPAssignments(llvm::Value *V, FuncSet &S)
{
	llvm::SmallPtrSet<llvm::Value *, 4> Visited;
	return collectFPAssignments(V, S, Visited);
}

bool CallInfo::collectFPAssignments(llvm::Value *V, FuncSet &S, 
					llvm::SmallPtrSet<llvm::Value *, 4> &Visited)
{	
	if (!Visited.insert(V))
		return false;

	// real function, S = S + {F}
	if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(V)) {
		if (!F->empty())
			return S.insert(F);
		llvm::Function *def = LinkerCtx->findGlobalFunction(F->getName());
		return S.insert(def ? def : F);
	}

	// bitcast, ignore the cast
	if (llvm::BitCastInst *B = llvm::dyn_cast<llvm::BitCastInst>(V))
		return collectFPAssignments(B->getOperand(0), S, Visited);
	
	// const bitcast, ignore the cast
	if (llvm::ConstantExpr *C = llvm::dyn_cast<llvm::ConstantExpr>(V)) {
		if (C->isCast())
			return collectFPAssignments(C->getOperand(0), S, Visited);
	}
	
	// PHI node, recursively collect all incoming values
	if (llvm::PHINode *P = llvm::dyn_cast<llvm::PHINode>(V)) {
		bool changed = false;
		for (unsigned i = 0; i != P->getNumIncomingValues(); ++i)
			changed |= collectFPAssignments(P->getIncomingValue(i), S, Visited);
		return changed;
	}
	
	// select, recursively collect both paths
	if (llvm::SelectInst *SI = llvm::dyn_cast<llvm::SelectInst>(V)) {
		bool changed = false;
		changed |= collectFPAssignments(SI->getTrueValue(), S, Visited);
		changed |= collectFPAssignments(SI->getFalseValue(), S, Visited);
		return changed;
	}
	
	// arguement, S = S + FuncPtrs[arg.ID]
	if (llvm::Argument *A = llvm::dyn_cast<llvm::Argument>(V))
		return unionFuncPtrs(S, getArgID(A));
	
	// return value, S = S + FuncPtrs[ret.ID]
	if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(V)) {
		llvm::Function *CF = CI->getCalledFunction();
		// TODO: handle indirect calls
		if (!CF)
			return false;
		return unionFuncPtrs(S, getRetID(CF));
	}
	
	// loads, S = S + FuncPtrs[struct.ID]
	if (llvm::LoadInst *L = llvm::dyn_cast<llvm::LoadInst>(V)) {
		llvm::MDNode *ID = L->getMetadata("id");
		if (!ID)
			return false;
		llvm::StringRef sID = 
			llvm::dyn_cast<llvm::MDString>(ID->getOperand(0))->getString();
		return unionFuncPtrs(S, sID);
	}
	
	// ignore other constant (usually null), inline asm and inttoptr
	if (llvm::isa<llvm::Constant>(V) 
		|| llvm::isa<llvm::InlineAsm>(V)
		|| llvm::isa<llvm::IntToPtrInst>(V))
		return false;
		
	llvm::dbgs() << "collectFPAssignment: unhandled value type:\n   ";
	V->dump();
	return false;
}


//
// Handle function pointer assignments in function body
//
bool CallInfo::collectFPAssignments(llvm::Function *F)
{
	bool changed = false;
	for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
		
		// handle store to function pointers
		if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(&*i)) {
			llvm::Value *V = SI->getValueOperand();
			llvm::MDNode *ID = SI->getMetadata("id");
			if (!ID || !isFunctionPointer(V->getType()))
				continue;
			llvm::StringRef sID = llvm::dyn_cast<llvm::MDString>
				(ID->getOperand(0))->getString();
			changed |= collectFPAssignments(V, LinkerCtx->FuncPtrs[sID]);
		}
		
		// handle arguement passing in call instructions
		if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(&*i)) {
			
			// ignore calls to inline asm or instrinsic functions
			if (CI->isInlineAsm())
				continue;
			if (CI->getCalledFunction() 
				&& CI->getCalledFunction()->isIntrinsic())
				continue;
			
			// collect possible callees
			FuncSet FS;
			if (!collectFPAssignments(CI->getCalledValue(), FS))
				continue;
			
			// Find function pointer arguments
			for (unsigned j = 0; j != CI->getNumArgOperands(); ++j) {
				llvm::Value *V = CI->getArgOperand(j);
				if (!isFunctionPointer(V->getType()))
					continue;
				
				// Find all possible assignments to the argument
				FuncSet VS;
				if (!collectFPAssignments(V, VS))
					continue;
				
				// Update argument FP-set for possible callees
				for (FuncSet::iterator k = FS.begin(), ke = FS.end(); 
					 k != ke; ++k) {
					std::string sID = "arg." + (*k)->getName().str()
						+ "." + llvm::Twine(j).str();
					changed |= unionFuncPtrs(LinkerCtx->FuncPtrs[sID], VS);
				}
			}
		}
		
		// handle return values
		if (llvm::ReturnInst *RI = llvm::dyn_cast<llvm::ReturnInst>(&*i)) {
			if (!isFunctionPointer(F->getReturnType()))
				continue;
			llvm::Value *V = RI->getReturnValue();
			std::string sID = "ret." + F->getName().str();
			changed |= collectFPAssignments(V, LinkerCtx->FuncPtrs[sID]);
		}
	}
	return changed;
}

//
// Iteratively update FuncPtrs mapping for the module until converge
//
bool CallInfo::collectFPAssignments(llvm::Module *M)
{
	int itr = 0;
	bool changed = true, ret = false;
	
	while (changed) {
		++itr;
		changed = false;
		for (llvm::Module::iterator i = M->begin(), e = M->end(); i != e; ++i)
			changed |= collectFPAssignments(&*i);
		ret |= changed;
	}
	return ret;
}

// Update Callers and Callees mapping
void CallInfo::collectCallers(llvm::Module *M)
{
	cerr << "CallInfo::collectCallers called \n" ;
	for (llvm::Module::iterator i = M->begin(), e = M->end(); i != e; ++i) {
		for (llvm::inst_iterator j = llvm::inst_begin(&*i),
			 je = llvm::inst_end(&*i); j != je; ++j) {
			if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(&*j)) {
				FuncSet &FS = LinkerCtx->Callees[CI];
				collectFPAssignments(CI->getCalledValue(), FS);
				for (FuncSet::iterator k = FS.begin(), ke = FS.end();
					 k != ke; ++k) {
					llvm::Function *F = *k;
					if (!F || F->empty())
						continue;
					LinkerCtx->Callers[F].push_back(CI);
				}
			}
		}
	}
}

