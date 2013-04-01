#define DEBUG_TYPE "hoist"
#include <llvm/Instructions.h>
#include <llvm/Pass.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/Dominators.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpander.h>
#include <llvm/Support/CFG.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <iostream>

using std::cerr;


STATISTIC(NumLoadHoisted, "Number of load instructions hoisted");

extern char &TrapAliasAnalysisID;

namespace {

// Hoist load instructions for more optimization oppurtunities
// to work around alias and mod/ref problems.
class Hoist : public llvm::FunctionPass {
public:
	static char ID;
	Hoist() : llvm::FunctionPass(ID) {
		llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
		llvm::initializeAliasAnalysisAnalysisGroup(Registry);
		llvm::initializeDominatorTreePass(Registry);
		llvm::initializeScalarEvolutionPass(Registry);
	}

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.addRequired<llvm::AliasAnalysis>();
		AU.addRequiredID(TrapAliasAnalysisID);
		AU.addRequired<llvm::DominatorTree>();
		AU.addRequired<llvm::ScalarEvolution>();
	}

	virtual bool runOnFunction(llvm::Function &F) {
		cerr << "Hoist::runOnFunction called\n" ;
		AA = 0;
		DT = 0;
		SE = 0;
                // Split BB for more oppurtunities since we only have
                // BB-level ScalarEvolution::dominates.
		for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i)
			if (llvm::CallInst *I = llvm::dyn_cast<llvm::CallInst>(&*i))
				i.getBasicBlockIterator() = llvm::SplitBlock(I->getParent(), I, this);
                // Hoist each load instruction.
		for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; )
			if (llvm::LoadInst *I = llvm::dyn_cast<llvm::LoadInst>(&*i++))
				visitLoadInst(I);
		return true;
	}

private:
	typedef llvm::SmallPtrSet<llvm::BasicBlock *, 32> BBSet;
	typedef llvm::AliasAnalysis::Location Location;

	llvm::AliasAnalysis *AA;
	llvm::DominatorTree *DT;
	llvm::ScalarEvolution *SE;

	bool visitLoadInst(llvm::LoadInst *I);
	void addBBOnPath(llvm::BasicBlock *BB, llvm::BasicBlock *BBToHoistTo,
	                 BBSet &BBSetOnPath);

	bool canBasicBlockModify(const llvm::BasicBlock *BB, const Location &Loc) {
		return canInstructionRangeModify(BB->front(), BB->back(), Loc);
	}
	bool canInstructionRangeModify(
		const llvm::Instruction &I1,
		const llvm::Instruction &I2,
                const Location &Loc) {
		assert(I1.getParent() == I2.getParent()
			&& "Instructions not in same basic block!");
		llvm::BasicBlock::const_iterator i = &I1, e = &I2;
		for (; i != e; ++i) {
			const llvm::Instruction *I = i;
			if (AA->getModRefInfo(I, Loc) & llvm::AliasAnalysis::Mod)
				return true;
		}
		return false;
	}
};

} // anonymous namespace

bool Hoist::visitLoadInst(llvm::LoadInst *I) {
	llvm::BasicBlock *BB = I->getParent();
	// Cannot deal with entry block for now.
	if (BB == &BB->getParent()->getEntryBlock())
		return false;

	if (!AA)
		AA = &getAnalysis<llvm::AliasAnalysis>();
	if (!DT)
		DT = &getAnalysis<llvm::DominatorTree>();
	if (!SE)
		SE = &getAnalysis<llvm::ScalarEvolution>();

	llvm::Value *V = I->getPointerOperand();
	if (!SE->isSCEVable(V->getType()))
		return false;

	// Verify that instructions before I in BB will not modify memory
	// location at V.
	Location Loc = AA->getLocation(I);
	if (canInstructionRangeModify(BB->front(), *I, Loc))
		return false;

	const llvm::SCEV *S = SE->getSCEV(V);
	// Recursively look for a candidate BB.
	for (;;) {
		llvm::BasicBlock *BBCandidate = BB->getUniquePredecessor();
		// If current BB has multiple predecessors, find the common dominator.
		if (!BBCandidate) {
			llvm::pred_iterator i = pred_begin(BB),
			                    e = pred_end(BB);
			// Entry block.
			if (i == e)
				break;
			llvm::BasicBlock *BB0 = *i;
			llvm::BasicBlock *BB1 = *++i;
			BBCandidate = DT->findNearestCommonDominator(BB0, BB1);
			for (++i; i != e; ++i)
				BBCandidate = DT->findNearestCommonDominator(BBCandidate, *i);
		}
		// Don't hoist into its own BB.
		if (BBCandidate == BB)
			break;
		// The expression should be defined at the candidate BB.
		if (!SE->dominates(S, BBCandidate))
			break;
		// Verify that no instruction modifies the memory location at V
		// from BBToHoistTo to BB.
		BBSet BBSetOnPath;
		for (llvm::pred_iterator i = pred_begin(BB), e = pred_end(BB); i != e; ++i)
			addBBOnPath(*i, BBCandidate, BBSetOnPath);
		for (BBSet::iterator i = BBSetOnPath.begin(), e = BBSetOnPath.end();
		     i != e; ++i) {
			if (canBasicBlockModify(*i, Loc)) {
				BBCandidate = 0;
				break;
			}
		}
		if (!BBCandidate)
			break;
		// Update BB to hoist to.
		BB = BBCandidate;
		// Test if we can move the instruction to the beginning of BB.
		if (canBasicBlockModify(BB, Loc))
			break;
	}

	if (BB == I->getParent())
		return false;

	// Rewrite.
	llvm::SCEVExpander Rewriter(*SE, "");
	llvm::TerminatorInst *TI = BB->getTerminator();
	llvm::Value *NewV = Rewriter.expandCodeFor(S, V->getType(), TI);
	NewV->setName(V->getName());
	I->setOperand(I->getPointerOperandIndex(), NewV);
	I->moveBefore(TI);
	++NumLoadHoisted;
	return true;
}

void Hoist::addBBOnPath(llvm::BasicBlock *BB, llvm::BasicBlock *BBToHoistTo,
                        BBSet &BBSetOnPath) {
	if (BB == BBToHoistTo)
		return;
	if (!BBSetOnPath.insert(BB))
		return;
	for (llvm::pred_iterator i = pred_begin(BB), e = pred_end(BB); i != e; ++i)
		addBBOnPath(*i, BBToHoistTo, BBSetOnPath);
}

char Hoist::ID;

static llvm::RegisterPass<Hoist>
X("hoist", "Hoist load and GEP instructions");

llvm::Pass *createHoistPass() {
	return new Hoist;
}
