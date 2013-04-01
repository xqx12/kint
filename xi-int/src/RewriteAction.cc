#include "RewriteAction.h"
#include "Trap.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Rewrite/Rewriter.h>
#include <llvm/ADT/Twine.h>

#include <clang/AST/ASTContext.h>
#include <iostream>

using std::cerr;

namespace {

#pragma mark RewriteVisitor

class RewriteVisitor : public clang::RecursiveASTVisitor<RewriteVisitor> {
public:
	RewriteVisitor(clang::Rewriter &R, clang::ASTContext &C) : R(R), C(C) { }

	// Disable traversing TypeLocs, otherwise some nodes may be traversed
	// more than once, e.g., in sizeof(char[1+x]).
	bool TraverseType(clang::QualType)   { return true; }
	bool TraverseTypeLoc(clang::TypeLoc) { return true; }

	// Annotate truncations.
	bool VisitCastExpr(clang::CastExpr *E) {
		//rewriteCast(E);
		return true;
	}

	bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr *E) {
		clang::Expr *Base = E->getBase()->IgnoreImpCasts();
		clang::QualType BaseTy = Base->getType();
		clang::Expr *Idx = E->getIdx();
		if (const clang::ConstantArrayType *T = C.getAsConstantArrayType(BaseTy)) {
			// Skip a[0].
			if (uint64_t Size = T->getSize().getZExtValue()) {
				// Skip a[1] if a is the last member of a struct.
				if (Size > 1 || !isLastField(Base)) {
					rewriteIndex(Idx, "index" + llvm::Twine(Size));
					return true;
				}
			}
		}
		rewriteIndex(Idx, "index");
		return true;
	}

private:
	clang::Rewriter &R;
	clang::ASTContext &C;
	llvm::StringMap<clang::FunctionDecl *> FunctionDeclCache;

	void annotate(clang::Expr *E, const llvm::Twine &Anno) {
		uint64_t Size = C.getTypeSize(E->getType());
		const llvm::Twine &Pre = TRAP_ANNOTATE_PREFIX + llvm::Twine(Size) + "(";
		const llvm::Twine &Post = ", \"" + Anno + "\")";
		if (R.InsertText(E->getLocStart(), Pre.str())
		    || R.InsertTextAfterToken(E->getLocEnd(), Post.str())) {
			E->dump();
			llvm::report_fatal_error("Rewriting failed!");
		}
	}

	void rewriteCast(clang::CastExpr *E) {
		clang::Expr *Sub = E->getSubExpr();
		clang::QualType DstTy = E->getType(), SrcTy = Sub->getType();
		if (!DstTy->isIntegerType()
		    || !SrcTy->isIntegerType()
		    || E->isEvaluatable(C))
			return;
		uint64_t DstSize = C.getTypeSize(DstTy);
		uint64_t SrcSize = C.getTypeSize(SrcTy);
		// Only deal with type truncation.
		if (DstSize >= SrcSize)
			return;
		const llvm::Twine &Anno = (llvm::isa<clang::ImplicitCastExpr>(E)? "i": "e")
			+ llvm::Twine("cast")
			+ TRAP_SEPARATOR "i" + llvm::Twine(DstSize)
			+ TRAP_SEPARATOR "i" + llvm::Twine(SrcSize);
		annotate(Sub, Anno);
	}

	void rewriteIndex(clang::Expr *E, const llvm::Twine &Op) {
		clang::QualType T = E->getType();
		if (!T->isIntegerType() || E->isEvaluatable(C))
			return;
		const llvm::Twine &Anno = llvm::Twine(Op)
			+ TRAP_SEPARATOR "i" + llvm::Twine(C.getTypeSize(T));
		annotate(E, Anno);
	}

	bool isLastField(clang::Expr *E) {
		if (clang::MemberExpr *ME = llvm::dyn_cast<clang::MemberExpr>(E)) {
			if (clang::FieldDecl *FD = llvm::dyn_cast<clang::FieldDecl>(ME->getMemberDecl())) {
				clang::RecordDecl *RD = FD->getParent();
				clang::RecordDecl::field_iterator i;
				for (i = RD->field_begin(); *i != FD; ++i)
					;
				if (++i == RD->field_end())
					return true;
			}
		}
		return false;
	}
};

#pragma mark -
#pragma mark RewriteConsumer

class RewriteConsumer : public clang::ASTConsumer {
public:
	RewriteConsumer(llvm::raw_ostream &OS) : OS(OS) { }
	virtual void HandleTranslationUnit(clang::ASTContext &C) {
		R.setSourceMgr(C.getSourceManager(), C.getLangOpts());
		clang::TranslationUnitDecl *TUD = C.getTranslationUnitDecl();
		RewriteVisitor(R, C).TraverseDecl(TUD);
		R.getEditBuffer(C.getSourceManager().getMainFileID()).write(OS);
	}
private:
	clang::Rewriter R;
	llvm::raw_ostream &OS;
};

} // anonymous namespace

#pragma mark -
#pragma mark RewriteAction

clang::ASTConsumer *
RewriteAction::CreateASTConsumer(clang::CompilerInstance &, llvm::StringRef) {
	cerr << "RewriteAction::CreateASTConsumer called\n" ;
	return new RewriteConsumer(OS);
}
