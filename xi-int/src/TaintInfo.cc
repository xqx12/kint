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
#include "TaintInfo.h"

// Check both local taint and global sources
bool TaintInfo::isTaint(llvm::Value *V)
{
	if (VTS.count(V) || VTS.count(V->stripPointerCasts()))
		return true;
	
	// if not in VTS, check external taint
	if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(V)) {
		// taint if any possible callee could return taint
		if (!CI->isInlineAsm() && LinkerCtx->Callees.count(CI)) {
			FuncSet &CEEs = LinkerCtx->Callees[CI];
			for (FuncSet::iterator i = CEEs.begin(), e = CEEs.end();
				 i != e; ++i) {
				if (LinkerCtx->Taints.count(getRetID(*i))) {
					VTS.insert(CI);
					return true;
				}
			}
		}
	} else {
		// arguments and loads
		std::string sID = getValueID(V);
		if (sID != "" && (LinkerCtx->Taints.count(sID))) {
			VTS.insert(V);
			return true;
		}
	}
	return false;
}

bool TaintInfo::isTaintSource(const std::string &sID)
{
	TaintSet::iterator it = LinkerCtx->Taints.find(sID);
	if (it != LinkerCtx->Taints.end())
		return it->second;
	return false;
}

bool TaintInfo::markTaint(const std::string &sID, bool isSource = false)
{
	if (sID == "")
		return false;
	return LinkerCtx->Taints.insert(std::make_pair(sID, isSource)).second;
}

bool TaintInfo::updateTaint(llvm::Module *M)
{
	int itr = 0;
	bool changed = true, ret = false;
	VTS.clear();

	while (changed) {
		++itr;
		changed = false;
		for (llvm::Module::iterator i = M->begin(), e = M->end(); i != e; ++i)
			changed |= updateTaint(&*i);
		ret |= changed;
	}
	return ret;
}

// find and mark taint source
bool TaintInfo::checkTaintSource(llvm::Instruction *I)
{
	bool changed = false;
	if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(I)) {
		llvm::Function *CF = CI->getCalledFunction();
		if (!CF)
			return false;
		
		// mark return value of __kint_taint() taint
		if (CF->getName() == "__kint_taint")
			VTS.insert(I);
		
		if (CF->getName() == "__kint_taint" ||
			CF->getName() == "copy_from_user" ||
			CF->getName() == "_copy_from_user" ||
			CF->getName() == "__copy_from_user") {
			
			// 1st arg is the dest pointer
			llvm::Value *Dst = CI->getArgOperand(0)->stripPointerCasts();
			assert(Dst->getType()->isPointerTy());
			llvm::PointerType *PTy = 
				llvm::dyn_cast<llvm::PointerType>(Dst->getType());

			// skip null pointer
			if (llvm::Constant *C =llvm::dyn_cast<llvm::Constant>(Dst))
				if (C->isNullValue())
					return false;

			// mark all struct members as tainted
			if (llvm::StructType *STy = 
				llvm::dyn_cast<llvm::StructType>(PTy->getElementType())) {
				for (unsigned i = 0; i < STy->getNumElements(); ++i)
					changed |= markTaint(LinkerCtx->getFieldID(STy, i), true);
			}

			// mark global var as tainted
			if (llvm::GlobalVariable *GV =
				llvm::dyn_cast<llvm::GlobalVariable>(Dst)) {
				changed |= markTaint(getVarID(GV), true);
			}
			VTS.insert(Dst);
		}		
	}
	return changed;
}

bool TaintInfo::checkTaintSource(llvm::Function *F)
{
	bool changed = false;
	// system call arguements
	if (F->getName().startswith("sys_") && !F->isVarArg()) {
		for (llvm::Function::arg_iterator i = F->arg_begin(), e = F->arg_end();
			 i != e; ++i) {
			changed |= markTaint(getArgID(&*i), true);
		}
	}
	return changed;
}

// Propagate taint within a function
bool TaintInfo::updateTaint(llvm::Function *F)
{
	bool changed = false;
	
	// Looking for taint sources in arguments
	changed |= checkTaintSource(F);
	
	for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
		bool tainted = false;
		llvm::Instruction *I = &*i;
		
		// Looking for taint sources
		changed |= checkTaintSource(I);
		
		// check if any operand is tainted
		for (unsigned j = 0; j < I->getNumOperands() && !tainted; ++j)
			tainted |= isTaint(I->getOperand(j));

		if (!tainted)
			continue;

		// update VTS and global taint
		VTS.insert(I);
		if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(I)) {
			if (llvm::MDNode *ID = SI->getMetadata("id")) {
				llvm::StringRef sID = llvm::dyn_cast<llvm::MDString>
					(ID->getOperand(0))->getString();
				changed |= markTaint(sID);
			}
			if (llvm::GlobalVariable *GV = 
				llvm::dyn_cast<llvm::GlobalVariable>(SI->getPointerOperand())) {
				changed |= markTaint(getVarID(GV));
			}
		} else if (llvm::isa<llvm::ReturnInst>(I)) {
			changed |= markTaint(getRetID(F));
		} else if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(I)) {
			if (!CI->isInlineAsm() && LinkerCtx->Callees.count(CI)) {
				FuncSet &CEEs = LinkerCtx->Callees[CI];
				for (FuncSet::iterator j = CEEs.begin(), je = CEEs.end();
					 j != je; ++j) {
					
					// skip vaarg and builtin functions
					if ((*j)->isVarArg() 
						|| (*j)->getName().find('.') != llvm::StringRef::npos)
						continue;
					
					for (unsigned a = 0; a < CI->getNumArgOperands(); ++a) {
						if (isTaint(CI->getArgOperand(a))) {
							// mark this arg tainted on all possible callees
							changed |= markTaint(getArgID(*j, a));
						}
					}
				}
				if (isTaint(CI))
					changed |= markTaint(getRetID(CI));
			}
		}
	}
	return changed;
}


