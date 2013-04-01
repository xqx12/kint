#pragma once

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Support/Registry.h>
#include "SMTSolver.h"

namespace llvm {
	class CallInst;
	class Module;
	class StringRef;
} // namespace llvm

class Constraint {
public:
	virtual SMTExpr run(SMTSolver &, llvm::Value *) = 0;
};

typedef llvm::Registry<Constraint> ConstraintRegistry;

/// This class provides annotations for function calls, arguments,
/// and structure fields.
class TrapLib {
public:
	explicit TrapLib(llvm::Module &);
	SMTExpr run(SMTSolver &, llvm::Value *V);

private:
	llvm::DenseMap<llvm::Value *, Constraint *> Constraints;
	llvm::StringMap<Constraint *> LoadConstraints;
};
