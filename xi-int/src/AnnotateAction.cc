#include "AnnotateAction.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclGroup.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/MultiplexConsumer.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/Path.h>
#include <llvm/Module.h>
#include <vector>
#include <iostream>

using std::cerr;
namespace {

#pragma mark AnnotateConsumer

class AnnotateConsumer : public clang::ASTConsumer {
protected:
	typedef llvm::SmallPtrSet<clang::RecordDecl *, 16> StructSet;
	StructList &SL;
	StructSet PendingStructs;
public:
	AnnotateConsumer(StructList &sl)
		: SL(sl) {	}

	virtual void HandleTagDeclDefinition(clang::TagDecl *D) {
		cerr << "AnnotateAction::HandleTagDeclDefinition called\n" ;
		if (!D->isCompleteDefinition())
			return;
		clang::RecordDecl *RD = llvm::dyn_cast<clang::RecordDecl>(D);
		if (!RD)
			return;
		
		// Skip builtin structs.
		if (RD->getName() == "__va_list_tag")
			return;
		
		// Postpond addAttr() to HandleTopLevelDecl(). The surrounding 
		// typedef (if any) cannot be resolved at this moment.
		PendingStructs.insert(RD);
	}
	
	virtual bool HandleTopLevelDecl(clang::DeclGroupRef D) {
		cerr << "AnnotateAction::HandleTopLevelDecl called\n" ;
		for (StructSet::iterator it = PendingStructs.begin(), 
			 ie = PendingStructs.end(); it != ie; ++it) {
			clang::RecordDecl *RD = *it;
			clang::ASTContext &C = RD->getASTContext();
			std::string RDName = getRecordID(RD);
			StructList::value_type SV;
			SV.push_back(RDName);
			for (clang::RecordDecl::field_iterator i = RD->field_begin(),
				 e = RD->field_end(); i != e; ++i) {
				clang::FieldDecl *FD = *i;
				
				// Encode field name with its type
				clang::QualType Ty = FD->getType();
				std::string FDName = FD->getNameAsString() + encodeTypeName(Ty);				
				SV.push_back(FDName);
				
				// Ignore bit fields for now.
				if (FD->isBitField())
					continue;
				
				// Only annotate integer and function pointer fields.
				if (!Ty->isFunctionPointerType() && !Ty->isIntegerType())
					continue;
				
				std::string Anno = "id:struct." + RDName + "." + FDName;				
				FD->addAttr(new (C) 
							clang::AnnotateAttr(FD->getSourceRange(), C, Anno));
			}
			SL.push_back(SV);
		}
		PendingStructs.clear();
		return true;
	}

	static std::string getRecordID(clang::RecordDecl *RD) {
		if (RD->getIdentifier()) {
			return RD->getNameAsString();
		} else if (clang::TypedefNameDecl *TDD = RD->getTypedefNameForAnonDecl()) {
			return TDD->getNameAsString();
		} else {
			return "anon." + encodeSourceLocation(
					RD->getASTContext().getSourceManager(), RD->getLocation());
		}
	}
	
	static std::string encodeSourceLocation(clang::SourceManager &SM, 
											clang::SourceLocation Loc) {
		assert(Loc.isValid());
		// See getDiagnosticPresumedLoc()
		while (Loc.isMacroID()) {
			// See skipToMacroArgExpansion()
			for (clang::SourceLocation L = Loc; L.isMacroID();
				 L = SM.getImmediateSpellingLoc(L)) {
				if (SM.isMacroArgExpansion(L)) {
					Loc = L;
					break;
				}
			}
			// See getImmediateMacroCallerLoc()
			if (Loc.isMacroID()) {
				if (SM.isMacroArgExpansion(Loc))
					Loc = SM.getImmediateSpellingLoc(Loc);
				else
					Loc = SM.getImmediateExpansionRange(Loc).first;
			}
		}
		
		// Use file.line for location.
		clang::PresumedLoc PLoc = SM.getPresumedLoc(Loc);
		return llvm::sys::path::filename(PLoc.getFilename()).str()
			+ "." + llvm::Twine(PLoc.getLine()).str();
	}
	
	const char *encodeTypeName(const clang::QualType Ty) const {
		if (Ty->isFunctionPointerType())
			return ".FPtr";
		if (!Ty->isBuiltinType() || !Ty->isIntegerType())
			return "";
		switch (Ty->getAs<clang::BuiltinType>()->getKind()) {
			case clang::BuiltinType::Bool:              return ".Bool";
			case clang::BuiltinType::Char_S:
			case clang::BuiltinType::Char_U:            return ".Char";
			case clang::BuiltinType::SChar:             return ".SChar";
			case clang::BuiltinType::Short:             return ".Short";
			case clang::BuiltinType::Int:               return ".Int";
			case clang::BuiltinType::Long:              return ".Long";
			case clang::BuiltinType::LongLong:          return ".LongLong";
			case clang::BuiltinType::Int128:            return ".Int128";
			case clang::BuiltinType::UChar:             return ".UChar";
			case clang::BuiltinType::UShort:            return ".UShort";
			case clang::BuiltinType::UInt:              return ".UInt";
			case clang::BuiltinType::ULong:             return ".ULong";
			case clang::BuiltinType::ULongLong:         return ".ULongLong";
			case clang::BuiltinType::UInt128:           return ".UInt128";
			case clang::BuiltinType::WChar_S:
			case clang::BuiltinType::WChar_U:           return ".WChar";
			case clang::BuiltinType::Char16:            return ".Char16";
			case clang::BuiltinType::Char32:            return ".Char32";
			default:	llvm_unreachable("Invalid integer type.");		
		}
	}

};


} // anonymous namespace

#pragma mark -
#pragma mark AnnotateAction

void AnnotateAction::addAnnotationMD(llvm::Module *M)
{
	llvm::LLVMContext &C = M->getContext();
	
	llvm::NamedMDNode *NMD = M->getOrInsertNamedMetadata("cint.structs");
	for (StructList::iterator i = SL.begin(), e = SL.end(); i != e; ++i) {
		std::vector<llvm::Value *> vals;		
		for (StructList::value_type::iterator j = i->begin(), je = i->end();
			 j != je; ++j) {
			vals.push_back(llvm::MDString::get(C, *j));
		}
		llvm::MDNode *MD = llvm::MDNode::get(C, vals);
		NMD->addOperand(MD);
	}
}

llvm::Module * AnnotateAction::takeModule()
{
	llvm::Module *M = this->CodeGenAction::takeModule();
	if (M)
		addAnnotationMD(M);
	return M;
}

clang::ASTConsumer *
AnnotateAction::CreateASTConsumer(clang::CompilerInstance &CI,
                                   llvm::StringRef InFile) {
	cerr << "AnnotateAction::CreateASTConsumer called\n" ;
	
	std::vector<clang::ASTConsumer *> C;
	C.push_back(new AnnotateConsumer(SL));
	C.push_back(this->CodeGenAction::CreateASTConsumer(CI, InFile));
	return new clang::MultiplexConsumer(C);
}
