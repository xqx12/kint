#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/GlobalVariable.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/Support/ConstantRange.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace {

struct SysctlRange : ModulePass {
	static char ID;
	SysctlRange() : ModulePass(ID) { }

	virtual void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.setPreservesAll();
	}

	virtual bool runOnModule(Module &);

private:
	void extractCtlTable(ConstantStruct *);
};

} // anonymous namespace

bool SysctlRange::runOnModule(Module &M) {
	StructType *T = M.getTypeByName("struct.ctl_table");
	if (T) {
		Module::global_iterator i, e;
		for (i = M.global_begin(), e = M.global_end(); i != e; ++i) {
			GlobalVariable *GV = dyn_cast<GlobalVariable>(i);
			if (GV && GV->hasInitializer()) {
				ConstantArray *CA = dyn_cast<ConstantArray>(GV->getInitializer());
				if (CA && CA->getType()->getElementType() == T) {
					ConstantArray::op_iterator oi, oe;
					for (oi = CA->op_begin(), oe = CA->op_end(); oi != oe; ++oi) {
						if (ConstantStruct *CS = dyn_cast<ConstantStruct>(oi))
							extractCtlTable(CS);
					}
				}
			}
		}
	}
	return false;
}

// This is used to strip a cast from the type of a global variable to `void *'.
template <typename T, typename S>
static T *strip_cast(S *V) {
	return dyn_cast<T>(V->stripPointerCasts());
}

static ConstantInt *getInitializer(GlobalVariable *V) {
	if (!V->hasInitializer())
		return 0;
	Constant *C = V->getInitializer();
	if (ConstantInt *CI = dyn_cast<ConstantInt>(C))
		return CI;
	if (ConstantAggregateZero *CAZ = dyn_cast<ConstantAggregateZero>(C))
		return dyn_cast<ConstantInt>(CAZ->getSequentialElement());
	if (ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(C))
		if (CDS->getNumElements() == 1)
			return dyn_cast<ConstantInt>(CDS->getElementAsConstant(0));
	return 0;
}

void SysctlRange::extractCtlTable(ConstantStruct *C) {
	unsigned n = C->getNumOperands();
	Constant *V = strip_cast<Constant>(C->getOperand(1));
	Function *Handler = strip_cast<Function>(C->getOperand(n - 4));
	GlobalVariable *MinV = strip_cast<GlobalVariable>(C->getOperand(n - 2));
	GlobalVariable *MaxV = strip_cast<GlobalVariable>(C->getOperand(n - 1));
	if (!V || !Handler || !MinV || !MaxV)
		return;
	// proc_dointvec_minmax
	// proc_doulongvec_minmax
	if (!Handler->getName().endswith("_minmax"))
		return;
	ConstantInt *Min = getInitializer(MinV);
	ConstantInt *Max = getInitializer(MaxV);
	if (!Min || !Max)
		return;
	// The range should be in the form of [min, max + 1).
	ConstantRange Range(Min->getValue(), Max->getValue() + 1);
	// V should be either a global variable or a struct field.
	if (V->hasName())
		llvm::errs() << V->getName();
	else
		llvm::errs() << *V;
	llvm::errs() << ":\t" << Range << "\n";
}

char SysctlRange::ID;

static RegisterPass<SysctlRange>
X("sysctl-range", "Extract sysctl ranges", false, true);
