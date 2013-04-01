#define DEBUG_TYPE "trap-linux"
#include "Trap.h"
#include <llvm/Constants.h>
#include <llvm/Instructions.h>
#include <llvm/Module.h>
#include <llvm/Pass.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Support/InstIterator.h>
#include <iostream>

using std::cerr;

namespace {

struct Param {
	const char *Name;
	unsigned Index;
};

static Param Allocs[] = {
	{"dma_alloc_from_coherent", 1},
	{"__kmalloc", 0},
	{"kmalloc", 0},
	{"__kmalloc_node", 0},
	{"kmalloc_node", 0},
	{"kzalloc", 0},
	{"kcalloc", 0},
	{"kcalloc", 1},
	{"kmemdup", 1},
	{"memdup_user", 1},
	{"pci_alloc_consistent", 1},
	{"__vmalloc", 0},
	{"vmalloc", 0},
	{"vmalloc_user", 0},
	{"vmalloc_node", 0},
	{"vzalloc", 0},
	{"vzalloc_node", 0},
	{0, 0}
};

static Param Sizes[] = {
	{"copy_from_user", 2},
	{"copy_in_user", 2},
	{"copy_to_user", 2},
	{"dma_free_coherent", 1},
	{"llvm.memcpy.p0i8.p0i8.i32", 2},
	{"llvm.memcpy.p0i8.p0i8.i64", 2},
	{"llvm.memmove.p0i8.p0i8.i32", 2},
	{"llvm.memmove.p0i8.p0i8.i64", 2},
	{"llvm.memset.p0i8.i32", 2},
	{"llvm.memset.p0i8.i64", 2},
	{"memcpy", 2},
	{"memcpy_fromiovec", 2},
	{"memcpy_fromiovecend", 2},
	{"memcpy_fromiovecend", 3},
	{"memcpy_toiovec", 2},
	{"memcpy_toiovecend", 2},
	{"memcpy_toiovecend", 3},	
	{"memmove", 2},
	{"memset", 2},
	{"pci_free_consistent", 1},
	{"sock_alloc_send_skb", 1},
	{"sock_alloc_send_pskb", 1},
	{"sock_alloc_send_pskb", 2},
	{0, 0}
};

static const char *ReadNoneFuncs[] = {
	"is_vmalloc_addr",
	0
};

static const char *ReadOnlyFuncs[] = {
	"_cond_resched",
	"timeval_to_ktime",
	"printk",
	0
};

// Remove implementation details to reduce noise,
// or our tool might report multiple integer errors
// at the same place.
class TrapLinux : public llvm::ModulePass {
public:
	static char ID;
	TrapLinux() : llvm::ModulePass(ID) { }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const {
		AU.setPreservesCFG();
	}

	virtual bool runOnModule(llvm::Module &M) {
		cerr << "TrapLinux::runOnModule called\n" ;
		deleteBody(M);
		markReadOnly(M);
		markNoAlias(M);
		markNoCapture(M);
		markIn(M);
		generateTrap(M);
		return true;
	}

private:
	void deleteBody(llvm::Module &);
	void deleteBody(llvm::Module &, const char *Name);
	void markReadNone(llvm::Module &);
	void markReadOnly(llvm::Module &);
	void markNoAlias(llvm::Module &);
	void markNoCapture(llvm::Module &M);
	void markIn(llvm::Module &M);
	void generateTrap(llvm::Module &);
	void generateUnaryTrap(const char *Trap, Param *, llvm::CallInst *);
};

} // anonymous namespace

void TrapLinux::deleteBody(llvm::Module &M) {
	static const char *Deletes[] = {
		0
	};
	for (Param *p = Allocs; p->Name; ++p)
		deleteBody(M, p->Name);
	for (Param *p = Sizes; p->Name; ++p)
		deleteBody(M, p->Name);
	for (const char **p = ReadNoneFuncs; *p; ++p)
		deleteBody(M, *p);
	for (const char **p = ReadOnlyFuncs; *p; ++p)
		deleteBody(M, *p);
	for (const char **p = Deletes; *p; ++p)
		deleteBody(M, *p);
}

void TrapLinux::deleteBody(llvm::Module &M, const char *Name) {
	if (llvm::Function *F = M.getFunction(Name))
		if (!F->empty())
			F->deleteBody();
}

void TrapLinux::markReadNone(llvm::Module &M) {
	for (const char **p = ReadNoneFuncs; *p; ++p) {
		if (llvm::Function *F = M.getFunction(*p))
			F->setDoesNotAccessMemory();
		}
}

void TrapLinux::markReadOnly(llvm::Module &M) {
	for (const char **p = ReadOnlyFuncs; *p; ++p) {
		if (llvm::Function *F = M.getFunction(*p))
			F->setOnlyReadsMemory();
		}
}

void TrapLinux::markNoAlias(llvm::Module &M) {
	// Set restrict/noalias to all parameters of pointer types.
	for (llvm::Module::iterator fi = M.begin(), fe = M.end(); fi != fe; ++fi) {
		if (fi->empty())
			continue;
		for (llvm::Function::arg_iterator i = fi->arg_begin(), e = fi->arg_end(); i != e; ++i)
			if (i->getType()->isPointerTy())
				i->addAttr(llvm::Attribute::NoAlias);
	}
	// Set __attribute__((malloc)).
	for (const Param *p = Allocs; p->Name; ++p)
		if (llvm::Function *F = M.getFunction(p->Name))
			if (F->getReturnType()->isPointerTy())
				F->setDoesNotAlias(0);
}

void TrapLinux::markNoCapture(llvm::Module &M) {
	static const char *NCs[] = {
		"copy_from_user",
		"copy_in_user",
		"copy_to_user",
		0
	};
	for (const char **p = NCs; *p; ++p) {
		llvm::Function *F = M.getFunction(*p);
		if (!F)
			continue;
		for (llvm::Function::arg_iterator i = F->arg_begin(), e = F->arg_end(); i != e; ++i)
			if (i->getType()->isPointerTy())
				i->addAttr(llvm::Attribute::NoCapture);
	}
}

void TrapLinux::markIn(llvm::Module &M) {
	static Param Ins[] = {
		{"copy_from_user", 1},
		{"copy_in_user", 1},
		{"copy_to_user", 1},
		{"llvm.memcpy.p0i8.p0i8.i32", 1},
		{"llvm.memcpy.p0i8.p0i8.i64", 1},
		{"llvm.memmove.p0i8.p0i8.i32", 1},
		{"llvm.memmove.p0i8.p0i8.i64", 1},
		{"memcpy", 1},
		{"memmove", 1},
		{0, 0}
	};
	llvm::NamedMDNode *NMD = M.getOrInsertNamedMetadata(TRAP_ARG_ANNOTATIONS);
	llvm::LLVMContext &VMCtx = M.getContext();
	llvm::IntegerType *T = llvm::Type::getInt64Ty(VMCtx);
	llvm::MDString *MDIn = llvm::MDString::get(VMCtx, "in");
	for (const Param *p = Ins; p->Name; ++p) {
		llvm::Function *F = M.getFunction(p->Name);
		if (!F)
			continue;
		unsigned Idx = p->Index;
		assert(F->arg_size() > Idx);
		llvm::Value *Args[] = {F, llvm::ConstantInt::get(T, Idx), MDIn};
		llvm::MDNode *MD = llvm::MDNode::get(VMCtx, Args);
		NMD->addOperand(MD);
	}
}

void TrapLinux::generateTrap(llvm::Module &M) {
	for (llvm::Module::iterator fi = M.begin(), fe = M.end(); fi != fe; ++fi) {
		for (llvm::inst_iterator i = inst_begin(fi), e = inst_end(fi); i != e; ++i) {
			llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(&*i);
			if (CI && CI->getCalledFunction()) {
				generateUnaryTrap("size", Sizes, CI);
				generateUnaryTrap("alloc", Allocs, CI);
			}
		}
	}
}

void TrapLinux::generateUnaryTrap(const char *Trap, Param *Params, llvm::CallInst *I) {
	llvm::StringRef Name = I->getCalledFunction()->getName();
	for (Param *p = Params; p->Name; ++p) {
		if (Name != p->Name)
			continue;
		llvm::Value *V = I->getArgOperand(p->Index);
		while (llvm::ZExtInst *ZEI = llvm::dyn_cast<llvm::ZExtInst>(V))
			V = ZEI->getOperand(0);
		assert(V->getType()->isIntegerTy());
		const llvm::Twine &FName = llvm::Twine(TRAP_PREFIX)
			+ Trap
			+ TRAP_SEPARATOR "i"
			+ llvm::Twine(V->getType()->getPrimitiveSizeInBits());
		insertTrapCall(FName, V, I);
	}
}

char TrapLinux::ID;

static llvm::RegisterPass<TrapLinux>
X("trap-linux", "Generate traps for the Linux kernel");

llvm::Pass *createTrapLinuxPass() {
	return new TrapLinux;
}
