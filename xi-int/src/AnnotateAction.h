#pragma once

#include <clang/AST/Decl.h>
#include <clang/CodeGen/BackendUtil.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <vector>
#include <map>

typedef llvm::SmallVector<llvm::SmallVector<std::string, 8>, 32> StructList;

// Emit code with annotations.
class AnnotateAction : public clang::CodeGenAction {
private:
	StructList SL;
	void addAnnotationMD(llvm::Module *);
	
protected:	
	virtual clang::ASTConsumer *CreateASTConsumer(
		clang::CompilerInstance &CI,
		llvm::StringRef InFile
	);

public:
	AnnotateAction(clang::BackendAction Act, llvm::LLVMContext *VMCtx = 0)
		: clang::CodeGenAction(Act, VMCtx) { }
	llvm::Module * takeModule();
};
