#define DEBUG_TYPE "trap-satmp"
#include <llvm/Function.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

namespace cl = llvm::cl;

static cl::opt<unsigned>
ProcessLimit("j", cl::desc("<The maximum number of workers>"), cl::init(1)); 

static cl::opt<unsigned>
TimeLimit("t", cl::desc("The maximum running time of each worker"),
	cl::value_desc("<seconds>"), cl::init(30)); 

extern char &TrapSatID;

namespace {

struct TrapSatMP : llvm::ModulePass {
	static char ID;
	TrapSatMP() : llvm::ModulePass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesAll();
		AU.addRequiredID(TrapSatID);
	}

	virtual bool runOnModule(llvm::Module &);
};

} // anonymous namespace

bool TrapSatMP::runOnModule(llvm::Module &M) {
	if (ProcessLimit == 0)
		ProcessLimit = 1;
	unsigned n = 0;
	for (llvm::Module::iterator i = M.begin(), e = M.end(); i != e; ++i) {
		llvm::Function &F = *i;
		if (F.empty())
			continue;
		if (n == ProcessLimit) {
			int status;
			wait(&status);
			--n;
		}
		++n;
		if (fork() == 0) {
			alarm(TimeLimit);
			getAnalysisID<llvm::Pass>(&TrapSatID, F);
			exit(0);
		}
	}
	for (; n; --n) {
		int status;
		wait(&status);
	}
	return false;
}

char TrapSatMP::ID;

static llvm::RegisterPass<TrapSatMP>
X("trap-satmp", "Check trap satisfiability (MP)", false, true);
