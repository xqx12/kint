#define DEBUG_TYPE "trap-sat"
#include "JSONWriter.h"
#include "SMTSolver.h"
#include "Trap.h"
#include "TrapLib.h"
#include <llvm/Instructions.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/ADT/OwningPtr.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Analysis/DebugInfo.h>
//#include </home/xqx/llvm/llvm/include/llvm/Analysis/DebugInfo.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Assembly/Writer.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Support/Path.h>
#include "llvm/Support/CommandLine.h"
#include <llvm/Target/TargetData.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <algorithm>
#include <list>
#include <map>

#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "Linker.h"
#include <iostream>


using std::cerr;

static llvm::cl::opt<bool>
Verbose("v", llvm::cl::desc("Print information about actions taken"));

static llvm::cl::opt<std::string>
ExtFilename("ext", llvm::cl::desc("external taint/range data"), 
			   llvm::cl::init("intrange.txt"),
			   llvm::cl::value_desc("filename"));

namespace {

struct TrapSat : llvm::FunctionPass, SMTSolver, llvm::SCEVVisitor<TrapSat, SMTExpr> {
	static char ID;
	TrapSat() : llvm::FunctionPass(ID), Invariant(0) {
		llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
		llvm::initializeScalarEvolutionPass(Registry);
	}

	// FunctionPass

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesAll();
		AU.addRequired<llvm::ScalarEvolution>();
	}

	virtual bool doInitialization(llvm::Module &M) {
		TD.reset(new llvm::TargetData(&M));
		TL.reset(new TrapLib(M));
		
		LC.reset(new LinkerContext());
		LC->parse(ExtFilename.c_str());
		if (Verbose) {
			llvm::errs() << "Load external taint/range from '"
				<< ExtFilename << "'\n";
			llvm::errs() << LC->Taints.size() << " taints, "
				<< LC->IntRanges.size() << " ranges.\n";
		}
		
		this->M = &M;
		return false;
	}

	virtual bool runOnFunction(llvm::Function &F);

	virtual void releaseMemory();

	// SCEVVisitor

	SMTExpr visitConstant(const llvm::SCEVConstant *S) {
		llvm::ConstantInt *CI = S->getValue();
		assert(CI->getBitWidth() <= 64);
		return bvconst(CI->getBitWidth(), CI->getZExtValue());
	}

	SMTExpr visitTruncateExpr(const llvm::SCEVTruncateExpr *S) {
		unsigned DstWidth = getBitWidth(S);
		return extract(DstWidth - 1, 0, get(S->getOperand()));
	}

	SMTExpr visitZeroExtendExpr(const llvm::SCEVZeroExtendExpr *S) {
		unsigned DstWidth = getBitWidth(S);
		unsigned SrcWidth = getBitWidth(S->getOperand());
		return zero_extend(DstWidth - SrcWidth, get(S->getOperand()));
	}

	SMTExpr visitSignExtendExpr(const llvm::SCEVSignExtendExpr *S) {
		unsigned DstWidth = getBitWidth(S);
		unsigned SrcWidth = getBitWidth(S->getOperand());
		return sign_extend(DstWidth - SrcWidth, get(S->getOperand()));
	}

	SMTExpr visitAddExpr(const llvm::SCEVAddExpr *S) {
		return mk_nary<&SMTSolver::bvadd>(S);
	}

	SMTExpr visitMulExpr(const llvm::SCEVMulExpr *S) {
		return mk_nary<&SMTSolver::bvmul>(S);
	}

	SMTExpr visitUDivExpr(const llvm::SCEVUDivExpr *S) {
		return bvudiv(get(S->getLHS()), get(S->getRHS()));
	}

	SMTExpr visitAddRecExpr(const llvm::SCEVAddRecExpr *S) {
#if 0
		const llvm::Loop *L = S->getLoop();
		llvm::PHINode *PN = L->getCanonicalInductionVariable();
		const llvm::SCEV *NS;
		// Fall back to start value if IndVar not available.
		if (PN)
			NS = S->evaluateAtIteration(SE->getUnknown(PN), *SE);
		else
			NS = S->getStart();
		assert(NS != S);
#else
		const llvm::SCEV *NS = S->getStart();
#endif
		return copy(get(NS));
	}

	SMTExpr visitSMaxExpr(const llvm::SCEVSMaxExpr *S) {
		return mk_nary<&SMTSolver::bvsmax>(S);
	}

	SMTExpr visitUMaxExpr(const llvm::SCEVUMaxExpr *S) {
		return mk_nary<&SMTSolver::bvumax>(S);
	}

	SMTExpr visitUnknown(const llvm::SCEVUnknown *S) {
		// SCEV doesn't deal with some instructions.
		llvm::Value *V = S->getValue();
		// Binary operators: sdiv/srem/urem/shl/lshr/ashr.
		if (llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(V))
			return visitBinaryOperator(BO);
		// ICmp.
		if (llvm::ICmpInst *ICI = llvm::dyn_cast<llvm::ICmpInst>(V))
			return visitICmpInst(ICI);
		// Select/ite.
		if (llvm::SelectInst *SI = llvm::dyn_cast<llvm::SelectInst>(V))
			return visitSelectInst(SI);
		// Overflow intrinsic
		if (llvm::ExtractValueInst *EVI = llvm::dyn_cast<llvm::ExtractValueInst>(V))
			return visitExtractValueInst(EVI);
		if (llvm::UndefValue *UV = llvm::dyn_cast<llvm::UndefValue>(V))
			return visitUndefValue(UV);
		// sizeof/alignof/offsetof.
		{
			llvm::Type *T;
			llvm::Constant *FieldNo;
			unsigned Width = getBitWidth(S);
			if (S->isSizeOf(T))
				return bvconst(Width, TD->getTypeAllocSize(T));
			if (S->isAlignOf(T))
				return bvconst(Width, TD->getABITypeAlignment(T));
			if (S->isOffsetOf(T, FieldNo)) {
				uint64_t Idx = llvm::cast<llvm::ConstantInt>(FieldNo)->getZExtValue();
				if (llvm::StructType *ST = llvm::dyn_cast<llvm::StructType>(T)) {
					const llvm::StructLayout *Layout = TD->getStructLayout(ST);
					return bvconst(Width, Layout->getElementOffset((unsigned)Idx));
				}
				if (llvm::SequentialType *ST = llvm::dyn_cast<llvm::SequentialType>(T)) {
					uint64_t ElemSize = TD->getTypeAllocSize(ST->getElementType());
					return bvconst(Width, ElemSize * Idx);
				}
				assert(0 && "Unknown offsetof!");
			}
		}
		return mk_fresh(V);
	}
	
	// looking for external range constraint from cintld
	SMTExpr getExternConstraint(llvm::Value *V) {
		IntRangeMap &IRM = LC->IntRanges;
		std::string sID = getValueID(V);
		if (sID != "") {
			IntRangeMap::iterator i = IRM.find(sID);
			// found global range
			if (i != IRM.end()) {
				llvm::ConstantRange CR = i->second;
				return mk_range(V, CR);
			}
		}
		// no constraint
		return NULL;
	}

private:
	typedef std::map<std::string, std::string> Dict;

	llvm::Module *M;

	llvm::OwningPtr<llvm::TargetData> TD;
	llvm::OwningPtr<TrapLib> TL;
	llvm::OwningPtr<LinkerContext> LC;

	llvm::ScalarEvolution *SE;

	typedef llvm::DenseMap<llvm::BasicBlock *, SMTExpr> GuardMap;
	typedef llvm::DenseMap<const llvm::SCEV *, SMTExpr> SCEVMap;
	GuardMap GuardCache;
	SCEVMap SCEVCache;

	typedef std::pair<
		const llvm::BasicBlock *, const llvm::BasicBlock *
	> Edge;
	llvm::SmallVector<Edge, 16> BackEdges;

	SMTExpr Invariant;

	bool isBackEdge(const Edge &E) const {
		return std::find(BackEdges.begin(), BackEdges.end(), E)
			!= BackEdges.end();
	}

	static void collectDbgInfo(llvm::Instruction *I, Dict &Item);

	SMTExpr getInitGuard(llvm::Function *F);
	SMTExpr getGuardFor(llvm::BasicBlock *BB);
	SMTExpr getTermGuard(llvm::TerminatorInst *I, llvm::BasicBlock *BB);
	SMTExpr getTermGuard(llvm::BranchInst *I, llvm::BasicBlock *BB);
	SMTExpr getTermGuard(llvm::SwitchInst *I, llvm::BasicBlock *BB);
	SMTExpr getPHIGuard(llvm::BasicBlock *BB, llvm::BasicBlock *Pred);

	static void dummy_handler(int) { }

#ifndef __APPLE__
	template <typename Stream>
	int timedQuery(SMTExpr Q, Stream &OS, unsigned timeout) {
		int ret = QUERY_FAILED;
		int r, signo, status;
		sigset_t mask, omask;
		pid_t pid;
		
		// block waiting signals
		sigemptyset(&mask);
		sigaddset(&mask, SIGCHLD);
		sigaddset(&mask, SIGALRM);
		sigprocmask(SIG_BLOCK, &mask, &omask);
		
		pid = fork();
		if (pid == -1)
			goto fail;
		
		// in child
		if (pid == 0) {
			// TODO: pass OS to parent
			r = SMTSolver::query(Q, SCEVCache, OS);
			exit(r);
		}
		
		alarm(timeout);
		signal(SIGCHLD, dummy_handler);
		sigwait(&mask, &signo);
		if (signo == SIGALRM)
			kill(pid, SIGKILL);
		
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
		else if (signo == SIGALRM)
			ret = QUERY_TIMEOUT;
		
	fail:
		// cancel alarm and restore signal mask
		alarm(0);
		sigprocmask(SIG_SETMASK, &omask, NULL);
		return ret;
	}
#endif
	
	template <typename Stream>
	int query(SMTExpr G, SMTExpr E, Stream &OS, unsigned timeout = 0) {
		SMTExpr Q = land(G, E);
		if (Invariant) {
			SMTExpr Tmp = land(Q, Invariant);
			release(Q);
			Q = Tmp;
		}
#ifdef __APPLE__
		int Res = SMTSolver::query(Q, SCEVCache, OS);
#else
		int Res;
		if (timeout == 0)
			Res = SMTSolver::query(Q, SCEVCache, OS);
		else
			Res = timedQuery(Q, OS, timeout);
#endif
		release(Q);
		return Res;
	}

	void toString(SMTExpr E, std::string &Str) {
		llvm::raw_string_ostream OS(Str);
		SMTSolver::print(OS, E);
	}

	SMTExpr get(const llvm::SCEV *S);

	virtual SMTExpr get(llvm::Value *V);

	SMTExpr getTrap(llvm::StringRef Op, llvm::Value *V);
	SMTExpr getTrap(llvm::StringRef Op, llvm::Value *V0, llvm::Value *V1);

	// Instructions that are not handled by SCEV.

	SMTExpr visitBinaryOperator(llvm::BinaryOperator *);

	SMTExpr visitICmpInst(llvm::ICmpInst *);

	SMTExpr visitSelectInst(llvm::SelectInst *);

	SMTExpr visitExtractValueInst(llvm::ExtractValueInst *);

	SMTExpr visitUndefValue(llvm::UndefValue *);
	
	template <typename Map>
	void rank_sink(llvm::Value *V, Map &M, llvm::StringRef type);
	
	void rank_taint(llvm::Value *V, Dict &D);
	std::string find_taint(llvm::Value *V);

	// Shortcuts.

	unsigned getBitWidth(llvm::Type *T) const {
		return (unsigned)SE->getTypeSizeInBits(T);
	}

	template <typename T>
	unsigned getBitWidth(T *V) const { return getBitWidth(V->getType()); }

	SMTExpr mk_fresh(llvm::Value *V) {
		std::string Name;
		{
			llvm::raw_string_ostream OS(Name);
			llvm::WriteAsOperand(OS, V, false, M);
		}
		return bvvar(getBitWidth(V), Name.c_str());
	}
	
	SMTExpr mk_range(llvm::Value *V, const llvm::ConstantRange &CR) {
		SMTExpr E = get(V);
		// empty or full set
		if (CR.getLower() == CR.getUpper())
			return NULL;
		
		unsigned bits = bvwidth(E);
		// single value
		if (CR.isSingleElement()) {
			SMTExpr val = bvconst(bits, CR.getLower().getLimitedValue());
			SMTExpr Eq = eq(E, val);
			release(val);
			return Eq;
		}
		
		// range
		SMTExpr lo = bvconst(bits, CR.getLower().getLimitedValue());
		SMTExpr up = bvconst(bits, CR.getUpper().getLimitedValue());
		SMTExpr e1 = bvuge(E, lo);
		SMTExpr e2 = bvult(E, up);
		SMTExpr R = CR.isWrappedSet() ? (lor(e1, e2)) : (land(e1, e2));
		release(lo);
		release(up);
		release(e1);
		release(e2);
		return R;
	}

	SMTExpr mk_iszero(SMTExpr R) {
		SMTExpr nonzero = bvredor(R);
		SMTExpr E = lnot(bv2bool(nonzero));
		release(nonzero);
		return E;
	}

	template <SMTExpr (SMTSolver::*F)(SMTExpr, SMTExpr)>
	SMTExpr mk_nary(const llvm::SCEVNAryExpr *S) {
		assert(S->getNumOperands() >= 2);
		llvm::SCEVNAryExpr::op_iterator i = S->op_begin(), e = S->op_end();
		SMTExpr E = (this->*F)(get(*i), get(*(i+1)));
		for (i += 2; i != e; ++i) {
			SMTExpr Tmp = (this->*F)(E, get(*i));
			release(E);
			E = Tmp;
		}
		return E;
	}
};

} // anonymous namespace


static inline void rank_append(std::string &rank, const std::string str)
{
	if (rank.find(str) == std::string::npos)
		rank = rank + str + " ";
}

template <typename Map>
void TrapSat::rank_sink(llvm::Value *V, Map &M, llvm::StringRef type) {
	typename Map::iterator i = M.find(V);
	if (i == M.end()) {
		if (llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(V)) {
			rank_sink(BO->getOperand(0), M, type);
			rank_sink(BO->getOperand(1), M, type);
		} else if (llvm::CastInst *CI = llvm::dyn_cast<llvm::CastInst>(V)) {
			rank_sink(CI->getOperand(0), M, type);
		}
		return;
	}
	typedef typename Map::mapped_type Set;
	Set &S = i->second;
	for (typename Set::iterator i = S.begin(), e = S.end(); i != e; ++i) {
		rank_append((**i)["rank"], type.str());
	}
}

std::string TrapSat::find_taint(llvm::Value *V)
{
	if (llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(V)) {
		std::string r = find_taint(BO->getOperand(0));
		return (r != "") ? r : find_taint(BO->getOperand(1));
	} else if (llvm::CastInst *CI = llvm::dyn_cast<llvm::CastInst>(V)) {
		return find_taint(CI->getOperand(0));
	} else if (llvm::SelectInst *SI = llvm::dyn_cast<llvm::SelectInst>(V)) {
		std::string r = find_taint(SI->getTrueValue());
		return (r != "") ? r : find_taint(SI->getFalseValue());		
	} else if (llvm::PHINode *PHI = llvm::dyn_cast<llvm::PHINode>(V)) {
		// FIXME: backedge loop?
		std::string r;
		for (unsigned i = 0, n = PHI->getNumIncomingValues(); i < n; ++i) {
			if (isBackEdge(Edge(PHI->getIncomingBlock(i), PHI->getParent())))
				continue;
			r = find_taint(PHI->getIncomingValue(i));
			if (r != "")
				return r;
		}
	} else {
		// load, call, args are possible taint source.
		std::string sID = getValueID(V);
		if (sID != "" && LC->Taints.count(sID))
			return sID;
	}
	return "";
}

void TrapSat::rank_taint(llvm::Value *V, Dict &D)
{
	std::string src = find_taint(V);
	if (src != "")
		rank_append(D["rank"], src);;
}

bool TrapSat::runOnFunction(llvm::Function &F) {
	cerr << "TrapSat::runOnFunction"  <<  "\n";
	typedef std::list<Dict> DictList;
	typedef llvm::SmallPtrSet<Dict *, 4> DictSet;
	llvm::DenseMap<llvm::Value *, DictSet> V2Sat;
	DictList Items;
	SE = &getAnalysis<llvm::ScalarEvolution>();
	// Collect back edges for computing guards.
	llvm::FindFunctionBackedges(F, BackEdges);
	// Initialize entry's guard according to TrapLib.
	GuardCache[&F.getEntryBlock()] = getInitGuard(&F);
	for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
		llvm::CallInst *I = llvm::dyn_cast<llvm::CallInst>(&*i);
		llvm::dbgs() <<  "CallInst: " << *I << "\n" ;
		if (!I)
			continue;
		llvm::StringRef Name = Trap::getName(I);
		llvm::dbgs() << "Name: " << Name <<  __FILE__ << ":" << __LINE__ << " \n";
		if (Name.empty())
			continue;
		if (Name.startswith("alloc") || Name.startswith("size"))
			rank_sink(I->getArgOperand(0), V2Sat, Name);
		llvm::BasicBlock *BB = I->getParent();
		// Trap condition.
		SMTExpr E;
		switch (I->getNumArgOperands()) {
		default: assert(0);
		case 1:
			// Skip alloc for now.
			if (Name.startswith("alloc"))
				continue;
			E = getTrap(Name, I->getArgOperand(0));
			break;
		case 2:
			E = getTrap(Name, I->getArgOperand(0), I->getArgOperand(1));
			break;
		}
		//llvm::dbgs() << "SMTExpr: " << *E << " at " << __FILE__ << ":" << __LINE__ << " \n";
		BoolectorSolver::print(llvm::dbgs() , E);
		Items.push_back(Dict());
		Dict &Item = Items.back();
		toString(E, Item["cond"]);
		Item["trap"] = Name;
		{
			llvm::raw_string_ostream OS(Item["argv"]);
			OS << '(';
			for (unsigned i = 0, n = I->getNumOperands() - 1; i != n; ++i) {
				if (i)
					OS << ", ";
				llvm::WriteAsOperand(OS, I->getOperand(i), false, M);
			}
			OS << ')';
		}
		Item["function"] = F.getName();
		Item["block"] = BB->getName();
	
					
		collectDbgInfo(I, Item);
		// Path guard.
		SMTExpr G = getGuardFor(BB);
		toString(G, Item["guard"]);
		if (Invariant)
			toString(Invariant, Item["invariant"]);
		// Solve SMT.
		llvm::raw_string_ostream OS(Item["model"]);
		int Status = query(G, E, OS, 5);
		switch (Status) {
		case QUERY_UNDEFINED: Item["status"] = "undef"; break;
		case QUERY_UNSAT: Item["status"] = "unsat"; break;
		case QUERY_SAT:  Item["status"] = "sat";   break;
		case QUERY_TIMEOUT: Item["status"] = "timeout"; break;
		default: Item["status"] = "failed"; break;
		}
		// Save SAT values for ranking.
		if (Status == QUERY_SAT) {
			for (unsigned j = 0, je = I->getNumArgOperands(); j != je; ++j) {
				llvm::Value *V = I->getOperand(j);
				V2Sat[V].insert(&Item);
				rank_taint(V, Item);
			}
		}
		release(E);
		
		//addbyxqx print item
		llvm::dbgs() <<  "Item: ------------------------\n"  ;
		llvm::dbgs() <<  "Item[cond]:----- \n "  << Item["cond"] << "\n" ;
		llvm::dbgs() <<  "Item[trap]:---- \n "  << Item["trap"] << "\n" ;
		llvm::dbgs() <<  "Item[function]:----- \n "  << Item["function"] << "\n" ;
		llvm::dbgs() <<  "Item[block]:---- \n "  << Item["block"] << "\n" ;
		llvm::dbgs() <<  "Item[guard]:---- \n "  << Item["guard"] << "\n" ;
		llvm::dbgs() <<  "Item[invariant]:---- \n "  << Item["invariant"] << "\n" ;
		llvm::dbgs() <<  "Item[model]:---- \n "  << Item["model"] << "\n" ;
		llvm::dbgs() <<  "Item[status]:---- \n "  << Item["status"] << "\n" ;
		
	}
	// Dump to file.
	std::string Filename = (llvm::sys::path::stem(
		F.getParent()->getModuleIdentifier()
	) + "." + F.getName() + ".json").str();
	
	llvm::dbgs() << "Writing to '" << Filename << "'\n";
	if (Verbose)
		llvm::errs() << "Writing '" << Filename << "'...\n";
	
	std::string ErrInfo;
	llvm::raw_fd_ostream OS(Filename.c_str(), ErrInfo);
	if (!ErrInfo.empty())
		llvm::report_fatal_error(ErrInfo);
	OS << "{ \"items\": ";
	JSONWriter<DictList>(OS) << Items;
	OS << "}\n";
	releaseMemory();
	return false;
}

void TrapSat::releaseMemory() {
	if (Invariant) {
		release(Invariant);
		Invariant = 0;
	}
	for (GuardMap::iterator i = GuardCache.begin(),
	     e = GuardCache.end(); i != e; ++i)
		release(i->second);
	for (SCEVMap::iterator i = SCEVCache.begin(),
	     e = SCEVCache.end(); i != e; ++i)
		release(i->second);
	GuardCache.clear();
	SCEVCache.clear();
	BackEdges.clear();
}

void TrapSat::collectDbgInfo(llvm::Instruction *I, Dict &Item) {
	const llvm::DebugLoc &DbgLoc = I->getDebugLoc();
	if (DbgLoc.isUnknown())
		return;
	llvm::MDNode *N = DbgLoc.getAsMDNode(I->getContext());
	llvm::DILocation Loc(N);
	assert(Loc.Verify());
	// We don't like see locations in headers.
	for (; Loc.getFilename().endswith(".h"); ) {
		llvm::DILocation OrigLoc = Loc.getOrigLocation();
		if (!OrigLoc.Verify())
			break;
		Loc = OrigLoc;
	}
	Item["line"] = llvm::utostr(Loc.getLineNumber());
	Item["column"] = llvm::utostr(Loc.getColumnNumber());
	Item["file"] = Loc.getFilename();
	Item["directory"] = Loc.getDirectory();
}

SMTExpr TrapSat::getInitGuard(llvm::Function *F) {
	if (SMTExpr G =	TL->run(*this, F))
		return G;
	return ltrue();
}

SMTExpr TrapSat::getGuardFor(llvm::BasicBlock *BB) {
	SMTExpr G = GuardCache.lookup(BB);
	if (!G) {
		// Initialize to false.
		G = lfalse();
		// The guard is the disjunction of predecessors' guards.
		llvm::pred_iterator i = llvm::pred_begin(BB), e = llvm::pred_end(BB);
		for (; i != e; ++i) {
			llvm::BasicBlock *Pred = *i;
			// Skip back edges.
			if (isBackEdge(Edge(Pred, BB)))
				continue;
			SMTExpr Term = getTermGuard(Pred->getTerminator(), BB);
			SMTExpr PN = getPHIGuard(BB, Pred);
			SMTExpr TermWithPN = land(Term, PN);
			release(Term);
			release(PN);
			SMTExpr Br = land(TermWithPN, getGuardFor(Pred));
			release(TermWithPN);
			SMTExpr Tmp = lor(G, Br);
			release(G);
			release(Br);
			G = Tmp;
		}
		GuardCache[BB] = G;
	}
	return G;
}

SMTExpr TrapSat::getPHIGuard(llvm::BasicBlock *BB, llvm::BasicBlock *Pred) {
	SMTExpr E = ltrue();
	llvm::BasicBlock::iterator i = BB->begin(), e = BB->end();
	for (; i != e; ++i) {
		llvm::PHINode *I = llvm::dyn_cast<llvm::PHINode>(i);
		if (!I)
			break;
		llvm::Value *V = I->getIncomingValueForBlock(Pred);
		// Generate I == V.
		SMTExpr PN = eq(get(I), get(V));
		SMTExpr Tmp = land(E, PN);
		release(E);
		release(PN);
		E = Tmp;
	}
	return E;
}

SMTExpr TrapSat::getTermGuard(llvm::TerminatorInst *I, llvm::BasicBlock *BB) {
	switch (I->getOpcode()) {
	default: I->dump(); llvm_unreachable("Unknown terminator!");
	case llvm::Instruction::Br:
		return getTermGuard(llvm::cast<llvm::BranchInst>(I), BB);
	case llvm::Instruction::Switch:
		return getTermGuard(llvm::cast<llvm::SwitchInst>(I), BB);
	}
}

SMTExpr TrapSat::getTermGuard(llvm::BranchInst *I, llvm::BasicBlock *BB) {
	if (I->isUnconditional())
		return ltrue();
	// Conditional branch.
	llvm::Value *V = I->getCondition();
	SMTExpr E = bv2bool(copy(get(V)));
	// True or false branch.
	if (I->getSuccessor(0) != BB) {
		assert(I->getSuccessor(1) == BB);
		SMTExpr Tmp = lnot(E);
		release(E);
		E = Tmp;
	}
	return E;
}

SMTExpr TrapSat::getTermGuard(llvm::SwitchInst *I, llvm::BasicBlock *BB) {
	llvm::Value *V = I->getCondition();
	SMTExpr L = get(V);
	if (I->getDefaultDest() != BB) {
		// Find all x = C_i for BB.
		SMTExpr E = lfalse();
		for (llvm::SwitchInst::CaseIt i = I->case_begin(), e = I->case_end(); i != e; ++i) {
			if (i.getCaseSuccessor() == BB) {
				llvm::ConstantInt *CI = i.getCaseValue();
				SMTExpr Cond = eq(L, get(CI));
				SMTExpr Tmp = lor(E, Cond);
				release(Cond);
				release(E);
				E = Tmp;
			}
		}
		return E;
	}
	// Compute guard for the default case.
	// i starts from 1; 0 is reserved for the default.
	SMTExpr E = lfalse();
	for (llvm::SwitchInst::CaseIt i = I->case_begin(), e = I->case_end(); i != e; ++i) {
		llvm::ConstantInt *CI = i.getCaseValue();
		SMTExpr Cond = eq(L, get(CI));
		SMTExpr Tmp = lor(E, Cond);
		release(Cond);
		release(E);
		E = Tmp;
	}
	SMTExpr NotE = lnot(E);
	release(E);
	return NotE;
}

SMTExpr TrapSat::get(const llvm::SCEV *S) {
	// Don't use something like
	//   SMTExpr &E = SCEVCache[S]
	// to update (S, E).  During visit the location may become invalid.
	SMTExpr E = SCEVCache.lookup(S);
	if (!E) {
		E = visit(S);
		SCEVCache[S] = E;
		if (const llvm::SCEVUnknown *U = llvm::dyn_cast<llvm::SCEVUnknown>(S)) {
			// combine external constraint and TrapLib constraint
			SMTExpr EC = getExternConstraint(U->getValue());
			SMTExpr LC = TL->run(*this, U->getValue());
			SMTExpr AE = NULL;
			if (EC != NULL && LC != NULL) {
				AE = land(EC, LC);
				release(EC);
				release(LC);
			} else
				AE = (EC != NULL) ? EC : LC;
				
			if (AE != NULL) {
				if (!Invariant)
					Invariant = AE;
				else {
					SMTExpr Tmp = land(Invariant, AE);
					release(Invariant);
					release(AE);
					Invariant = Tmp;
				}
			}
		}
	}
	assert(E);
	return E;
}

SMTExpr TrapSat::get(llvm::Value *V) {
	const llvm::SCEV *S = SE->getSCEV(V);
	// Special case: speed up select.  SCEV is not good at analyzing
	// select.  It may generate a huge expression and hang the solver.
	if (llvm::SelectInst *SI = llvm::dyn_cast<llvm::SelectInst>(V)) {
		SMTExpr E = SCEVCache.lookup(S);
		if (!E) {
			E = visitSelectInst(SI);
			SCEVCache[S] = E;
		}
		return E;
	}
	if (llvm::BinaryOperator *BO = llvm::dyn_cast<llvm::BinaryOperator>(V))
		if (llvm::isa<llvm::SelectInst>(BO->getOperand(0))
		    || llvm::isa<llvm::SelectInst>(BO->getOperand(1))) {
			SMTExpr E = SCEVCache.lookup(S);
			if (!E) {
				E = visitBinaryOperator(BO);
				SCEVCache[S] = E;
			}
			return E;
		}
	return get(S);
}

SMTExpr TrapSat::getTrap(llvm::StringRef Op, llvm::Value *V) {
	SMTExpr R = get(V);
	if (Op.startswith("udiv") || Op.startswith("urem")) {
		// R == 0
		return mk_iszero(R);
	}
	if (Op.startswith("shl") || Op.startswith("lshr") || Op.startswith("ashr")) {
		// R >=u DstWidth
		unsigned DstWidth = getBitWidth(V);
		SMTExpr Width = bvconst(DstWidth, DstWidth);
		SMTExpr E = bvuge(R, Width);
		release(Width);
		return E;
	}
	if (Op.startswith("index")) {
#if 1
		llvm::StringRef SizeStr = Op.substr(5, Op.find(TRAP_SEPARATOR) - 5);
		// R <s 0
		if (SizeStr.empty()) {
			unsigned Idx = bvwidth(R) - 1;
			return bv2bool(extract(Idx, Idx, R));
		}
		// R >u size
		// Allow 1 pass the last element.
		unsigned long long Size;
		if (SizeStr.getAsInteger(10, Size))
			llvm::report_fatal_error("Bad index: " + Op);
		SMTExpr Hi = bvconst(bvwidth(R), Size);
		SMTExpr E = bvugt(R, Hi);
		release(Hi);
		return E;
#else
		// R <s 0
		unsigned Idx = bvwidth(R) - 1;
		return bv2bool(extract(Idx, Idx, R));	
#endif
	}
	if (Op.startswith("size")) {
		// R <s 0
		unsigned Idx = bvwidth(R) - 1;
		return bv2bool(extract(Idx, Idx, R));
	}
	if (Op.startswith("icast") || Op.startswith("ecast")) {
		// R[SrcWidth-1:DstWidth] != 0 and
		// R[SrcWidth-1:DstWidth-1] != 1
		unsigned SrcWidth = getBitWidth(V), DstWidth;
		size_t Start = Op.find(TRAP_SEPARATOR) + 2, End = Op.rfind(TRAP_SEPARATOR);
		if (Op.substr(Start, End - Start).getAsInteger(10, DstWidth))
			llvm::report_fatal_error("Bad trunc operation: " + Op);
		assert(SrcWidth > DstWidth);
		SMTExpr C0Hi = extract(SrcWidth - 1, DstWidth, R);
		SMTExpr C0 = bv2bool(bvredor(C0Hi));
		release(C0Hi);
		SMTExpr C1Hi = extract(SrcWidth - 1, DstWidth - 1, R);
		SMTExpr C1 = bv2bool(bvredand(C1Hi));
		release(C1Hi);
		SMTExpr NotC1 = lnot(C1);
		release(C1);
		SMTExpr E = land(C0, NotC1);
		release(C0);
		release(NotC1);
		return E;
	}
	llvm::report_fatal_error("Unknown binary operation: " + Op);
}

SMTExpr TrapSat::getTrap(llvm::StringRef Op, llvm::Value *V0, llvm::Value *V1) {
	SMTExpr L = get(V0), R = get(V1);
	if (Op.startswith("sadd"))
		return bvadd_signed_overflow(L, R);
	if (Op.startswith("uadd"))
		return bvadd_unsigned_overflow(L, R);
	if (Op.startswith("ssub")) {
		if (llvm::ConstantInt *CI = llvm::dyn_cast<llvm::ConstantInt>(V0))
			if (CI->isZero())
				return bvneg_overflow(R);
		return bvsub_signed_overflow(L, R);
	}
	if (Op.startswith("usub"))
		return bvsub_unsigned_overflow(L, R);
	if (Op.startswith("smul"))
		return bvmul_signed_overflow(L, R);
	if (Op.startswith("umul"))
		return bvmul_unsigned_overflow(L, R);
	if (Op.startswith("sdiv") || Op.startswith("srem")) {
		SMTExpr IsOvfl = bvsdiv_overflow(L, R);
		SMTExpr IsZero = mk_iszero(R);
		SMTExpr E = lor(IsOvfl, IsZero);
		release(IsOvfl);
		release(IsZero);
		return E;
	}
	llvm::report_fatal_error("Unknown binary operation: " + Op);
}

SMTExpr TrapSat::visitBinaryOperator(llvm::BinaryOperator *I) {
	SMTExpr L = get(I->getOperand(0)), R = get(I->getOperand(1));
	switch (I->getOpcode()) {
	default: assert(0);
	case llvm::Instruction::Add:  return bvadd(L, R);
	case llvm::Instruction::Sub:  return bvsub(L, R);
	case llvm::Instruction::Mul:  return bvmul(L, R);
	case llvm::Instruction::UDiv: return bvudiv(L, R);
	case llvm::Instruction::SDiv: return bvsdiv(L, R);
	case llvm::Instruction::URem: return bvurem(L, R);
	case llvm::Instruction::SRem: return bvsrem(L, R);
	case llvm::Instruction::Shl:  return bvshl(L, R);
	case llvm::Instruction::LShr: return bvlshr(L, R);
	case llvm::Instruction::AShr: return bvashr(L, R);
	case llvm::Instruction::And:  return bvand(L, R);
	case llvm::Instruction::Or:   return bvor(L, R);
	case llvm::Instruction::Xor:  return bvxor(L, R);
	}
}

SMTExpr TrapSat::visitICmpInst(llvm::ICmpInst *I) {
	SMTExpr Cond, L = get(I->getOperand(0)), R = get(I->getOperand(1));
	switch (I->getPredicate()) {
	default: assert(0);
	case llvm::CmpInst::ICMP_EQ:  Cond = eq(L, R); break;
	case llvm::CmpInst::ICMP_NE:  Cond = ne(L, R); break;
	case llvm::CmpInst::ICMP_SGE: Cond = bvsge(L, R); break;
	case llvm::CmpInst::ICMP_SGT: Cond = bvsgt(L, R); break;
	case llvm::CmpInst::ICMP_SLE: Cond = bvsle(L, R); break;
	case llvm::CmpInst::ICMP_SLT: Cond = bvslt(L, R); break;
	case llvm::CmpInst::ICMP_UGE: Cond = bvuge(L, R); break;
	case llvm::CmpInst::ICMP_UGT: Cond = bvugt(L, R); break;
	case llvm::CmpInst::ICMP_ULE: Cond = bvule(L, R); break;
	case llvm::CmpInst::ICMP_ULT: Cond = bvult(L, R); break;
	}
	return bool2bv(Cond);
}

SMTExpr TrapSat::visitSelectInst(llvm::SelectInst *I) {
	return ite(
		bv2bool(get(I->getCondition())),
		get(I->getTrueValue()),
		get(I->getFalseValue())
	);
}

SMTExpr TrapSat::visitExtractValueInst(llvm::ExtractValueInst *I) {
	llvm::IntrinsicInst *II = llvm::dyn_cast<llvm::IntrinsicInst>(I->getAggregateOperand());
	if (!II || II->getCalledFunction()->getName().find(".with.overflow.")
			== llvm::StringRef::npos)
		return mk_fresh(I);
	SMTExpr L = get(II->getArgOperand(0));
	SMTExpr R = get(II->getArgOperand(1));
	assert(I->getNumIndices() == 1);
	assert(I->getIndices()[0] == 1 && "-overflow must be called!");
	switch (II->getIntrinsicID()) {
	default: II->dump(); assert(0 && "Unknown overflow!");
	case llvm::Intrinsic::sadd_with_overflow:
		return bool2bv(bvadd_signed_overflow(L, R));
	case llvm::Intrinsic::uadd_with_overflow:
		return bool2bv(bvadd_unsigned_overflow(L, R));
	case llvm::Intrinsic::ssub_with_overflow:
		return bool2bv(bvsub_signed_overflow(L, R));
	case llvm::Intrinsic::usub_with_overflow:
		return bool2bv(bvsub_unsigned_overflow(L, R));
	case llvm::Intrinsic::smul_with_overflow:
		return bool2bv(bvmul_signed_overflow(L, R));
	case llvm::Intrinsic::umul_with_overflow:
		return bool2bv(bvmul_unsigned_overflow(L, R));
	}
}

SMTExpr TrapSat::visitUndefValue(llvm::UndefValue *I) {
	std::string Str = "undef." + llvm::utostr((uint64_t)I);
	return bvvar(getBitWidth(I), Str.c_str());
}

char TrapSat::ID;

char &TrapSatID = TrapSat::ID;

static llvm::RegisterPass<TrapSat>
X("trap-sat", "Check trap satisfiability", false, true);

llvm::Pass *createTrapSatPass() {
	return new TrapSat;
}
