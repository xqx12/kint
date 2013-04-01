#pragma once

#include <llvm/Module.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Support/Debug.h>
#include <llvm/Analysis/DebugInfo.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/ConstantRange.h>
#include <llvm/Support/Path.h>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

typedef std::map<std::string, llvm::Function *> FuncMap;
typedef llvm::SmallPtrSet<llvm::Function *, 8> FuncSet;
typedef std::map<std::string, FuncSet> FuncPtrMap;
typedef llvm::SmallVector<llvm::CallSite, 16> CSList;
typedef llvm::DenseMap<llvm::Function *, CSList> CallerMap;
typedef llvm::DenseMap<llvm::CallInst *, FuncSet> CalleeMap;
typedef std::map<std::string, llvm::ConstantRange> IntRangeMap;
typedef std::map<std::string, bool /* is source */> TaintSet;
typedef std::map<std::string, std::vector<std::string> > FIDMap;


struct LinkerContext {
	// Map struct name to its field names
	FIDMap FID;

	// Map global function name to its definition
	FuncMap GlobalFuncs;
	
	// Map function pointers (in struct, arguments or return values)
	// to possible assignments
	FuncPtrMap FuncPtrs;
	
	// Range information
	IntRangeMap IntRanges;
	
	// Taints
	TaintSet Taints;

	// Map a function to all potential callers
	CallerMap Callers;
	
	// Map a callsite to all potential callees
	CalleeMap Callees;
	
	llvm::Function * findGlobalFunction(llvm::StringRef name)
	{
		FuncMap::iterator i = GlobalFuncs.find(name.str());
		if (i != GlobalFuncs.end())
			return i->second;
		return NULL;
	}
	
	llvm::StringRef getFieldID(llvm::StructType *STy, unsigned i) {
		FIDMap::iterator k = FID.find(STy->getName());
		if (k != FID.end() && i < k->second.size())
			return k->second[i];
		return "";
	}
	
	void outputTaints(llvm::raw_ostream &OS) {
		for (TaintSet::iterator i = Taints.begin(), 
			 e = Taints.end(); i != e; ++i) {
			if (i->first == "")
				continue;
			OS << "T " << i->first << (i->second ? " *" : "") << "\n";
		}
	}
	
	void outputRanges(llvm::raw_ostream &OS) {
		
		for (IntRangeMap::iterator i = IntRanges.begin(), 
			 e = IntRanges.end(); i != e; ++i) {
			if (i->first == "")
				continue;
			OS << "R " << i->first << " " 
				<< llvm::Twine(i->second.getBitWidth()).str() << "    "
				<< i->second.getLower() << " " << i->second.getUpper() << "\n";
		}
	}
	
	// TODO: this looks ugly
	void parse(const char * filename)
	{
		std::ifstream is(filename);
		
		std::string line, type, sID;
		unsigned bits;
		int64_t lo, hi;
		
		while (std::getline(is, line)) {
			std::istringstream iss(line);
			iss >> type >> sID;
			if (type == "T") {
				std::string tag;
				iss >> tag;
				Taints.insert(std::make_pair(sID, tag == "*"));
			}
			else if (type == "R") {
				iss >> bits >> lo >> hi;
				llvm::ConstantRange CR(llvm::APInt(bits, lo), llvm::APInt(bits, hi));
				IntRanges.insert(std::make_pair(sID, CR));
			}
		}
		is.close();
	}
	
	
	// debug
	void dumpFuncPtrs(llvm::raw_ostream &OS)
	{
		for (FuncPtrMap::iterator i = FuncPtrs.begin(), 
			 e = FuncPtrs.end(); i != e; ++i) {
			OS << i->first << "\n";
			FuncSet &v = i->second;
			for (FuncSet::iterator j = v.begin(), ej = v.end();
				 j != ej; ++j) {
				OS << "  " << ((*j)->hasInternalLinkage() ? "f" : "F")
					<< " " << (*j)->getName() << "\n";
			}
		}
	}
	
	// debug
	void dumpCallers(llvm::raw_ostream &OS)
	{
		OS << "\n\n*** Callers ***\n\n";
		for (CallerMap::iterator i = Callers.begin(), e = Callers.end();
				i != e; ++i) {
			OS << i->first->getName() << "\n";
			CSList &v = i->second;
			for (CSList::iterator j = v.begin(), ej = v.end(); j != ej; ++j) {
				llvm::CallInst *CI = 
				llvm::dyn_cast<llvm::CallInst>(j->getInstruction());
				OS << "  " << CI->getParent()->getParent()->getName() << "() : "
					<< CI->getParent()->getName() << " : " 
					<< CI->getName() << "\n";
				llvm::DILocation L(CI->getDebugLoc().getAsMDNode(CI->getContext()));
				OS << "      in " << L.getFilename() 
					<< ":" << llvm::utostr(L.getLineNumber()) << "\n";
			}
		}
	}

	
};

static inline std::string stripPath(const std::string &path)
{
	size_t pos;
	std::string ret = path;
	if ((pos = path.find("linux-3.")) != std::string::npos)
		if ((pos = path.find('/', pos)) != std::string::npos)
			ret.erase(0, pos + 1);
	if ((pos = ret.rfind('.')) != std::string::npos)
		ret.erase(pos);
	std::replace(ret.begin(), ret.end(), '/', '.');
	return ret;
}

static inline std::string getScopeName(llvm::GlobalValue *GV)
{
	if (GV->hasInternalLinkage())
		return "local." 
			+ stripPath(GV->getParent()->getModuleIdentifier())
			+ "." + GV->getName().str();
	return GV->getName().str();
}

static inline std::string getArgID(llvm::Argument *A)
{
	return "arg." + getScopeName(A->getParent()) + "." 
			+ llvm::Twine(A->getArgNo()).str();
}

static inline std::string getArgID(llvm::Function *F, unsigned i)
{
	return "arg." + getScopeName(F) + "." + llvm::Twine(i).str();
}


static inline std::string getRetID(llvm::Function *CF)
{
	return "ret." + getScopeName(CF);
}

static inline std::string getVarID(llvm::GlobalVariable *GV)
{
	return "var." + getScopeName(GV);
}

static inline std::string getValueID(llvm::Value *V);
static inline std::string getRetID(llvm::CallInst *CI)
{
	if (llvm::Function *CF = CI->getCalledFunction())
		return getRetID(CF);
	else {
		std::string sID = getValueID(CI->getCalledValue());
		if (sID != "")
			return "ret." + sID;
	}
	return "";
}

static inline std::string getValueID(llvm::Value *V)
{
	if (llvm::Argument *A = llvm::dyn_cast<llvm::Argument>(V))
		return getArgID(A);
	else if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(V))
		return getRetID(F);
	else if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(V)) {
		return getRetID(CI);
	} else if (llvm::isa<llvm::LoadInst>(V) || llvm::isa<llvm::StoreInst>(V)) {
		llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V);
		if (llvm::MDNode *MID = I->getMetadata("id"))
			return llvm::dyn_cast<llvm::MDString>(MID->getOperand(0))->getString();
		else {
			llvm::GlobalVariable *GV = NULL;
			if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(I))
				GV = llvm::dyn_cast<llvm::GlobalVariable>(LI->getPointerOperand());
			else if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(I))
				GV = llvm::dyn_cast<llvm::GlobalVariable>(SI->getPointerOperand());
			if (GV)
				return getVarID(GV);
		}
	}
	return "";
}

