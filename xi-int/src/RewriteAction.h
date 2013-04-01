#pragma once

#include <clang/Frontend/FrontendAction.h>

// Perform rewriting on a preprocessed source file.
class RewriteAction : public clang::ASTFrontendAction {
	llvm::raw_ostream &OS;
protected:
	virtual clang::ASTConsumer *CreateASTConsumer(
		clang::CompilerInstance &CI,
		llvm::StringRef InFile
	);
public:
	explicit RewriteAction(llvm::raw_ostream &OS): OS(OS) { }
};
