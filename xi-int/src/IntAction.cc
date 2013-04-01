#include "IntermediateFile.h"
#include "RewriteAction.h"
#include "AnnotateAction.h"
#include "Trap.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/MacroBuilder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendOptions.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Frontend/Utils.h>
#include <llvm/LLVMContext.h>
#include <llvm/InstrTypes.h>
#include <llvm/Operator.h>
#include <llvm/Module.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Scalar.h>

extern llvm::Pass *createLowerAnnotationPass();
extern llvm::Pass *createOverflowPass();
extern llvm::Pass *createLowerOverflowPass();
extern llvm::Pass *createHoistPass();

namespace {

class PreprocessAction : public clang::PreprocessorFrontendAction {
	llvm::raw_ostream &OS;
protected:
	virtual void ExecuteAction() {
		clang::CompilerInstance &CI = getCompilerInstance();
		clang::DoPrintPreprocessedInput(
			CI.getPreprocessor(), &OS,
            CI.getPreprocessorOutputOpts()
		);
	}
	virtual bool hasPCHSupport() const { return true; }
public:
	explicit PreprocessAction(llvm::raw_ostream &OS): OS(OS) { }
};

// Create a new compiler instance to perform the following steps.
// Preprocess -> Rewrite -> Generate LLVM bitcode
class IntAction : public clang::PluginASTAction {
public:
	virtual bool ParseArgs(
		const clang::CompilerInstance &CI,
		const std::vector<std::string> &arg
	) { return true; }
protected:
	virtual clang::ASTConsumer *CreateASTConsumer(
		clang::CompilerInstance &CI,
		llvm::StringRef InFile
	) {
		asm("int $3");
		llvm::errs() << "IntAction::CreateASTConsumer:" << InFile << "\n";
		// Create a separate compiler instance.
		clang::CompilerInstance Clang;

		// Make a copy of options.
		Clang.getInvocation() = CI.getInvocation();

		// Set up error reporting.
		clang::DiagnosticConsumer *DiagClient = new clang::TextDiagnosticPrinter(
			llvm::errs(), Clang.getDiagnosticOpts()
		);
		llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> DiagID(new clang::DiagnosticIDs);
		llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> Diags(
			new clang::DiagnosticsEngine(DiagID, DiagClient)
		);
		Clang.setDiagnostics(Diags.getPtr());
		clang::ProcessWarningOptions(Clang.getDiagnostics(), Clang.getDiagnosticOpts());

		// Avoid loading plugins again.
		Clang.getFrontendOpts().Plugins.clear();
		Clang.getFrontendOpts().PluginArgs.clear();
		Clang.getFrontendOpts().AddPluginActions.clear();
		Clang.getFrontendOpts().AddPluginArgs.clear();

		// Generate debugging information for better bug report.
		// addbyxqx20120806
		Clang.getCodeGenOpts().DebugInfo = 1;
		//Clang.getCodeGenOpts().DebugInfo = clang::CodeGenOptions::LimitedDebugInfo;
		//Clang.getCodeGenOpts().LimitDebugInfo = 0;

		act(Clang, InFile);

		// Return a dummy consumer; NULL would crash.
		return new clang::ASTConsumer;
	}

private:
	void act(clang::CompilerInstance &Clang, llvm::StringRef InFile);
	void forceWrap(llvm::Module &M);
};

} // anonymous namespace

void IntAction::act(clang::CompilerInstance &Clang, llvm::StringRef InFile) {

//		asm("int $3");
	llvm::errs() << "IntAction:act InFile=" << InFile << "\n";
	// Turn off all optimizations before TrapGen.
	Clang.getCodeGenOpts().DisableLLVMOpts = 1;

	// Disable -fwrapv to distinguish signed/unsigned before TrapGen.
	clang::LangOptions::SignedOverflowBehaviorTy SOB = Clang.getLangOpts().getSignedOverflowBehavior();
	Clang.getLangOpts().setSignedOverflowBehavior(
		clang::LangOptions::SOB_Undefined
	);

	// Shortcut.
	std::vector<clang::FrontendInputFile> &Inputs
		= Clang.getInvocation().getFrontendOpts().Inputs;

	llvm::StringRef OutputFileStem;
	if (Clang.getFrontendOpts().OutputFile.empty())
		OutputFileStem = llvm::sys::path::stem(InFile);
	else
		OutputFileStem = llvm::sys::path::stem(Clang.getFrontendOpts().OutputFile);

	// Define builtin macros and functions.
	IntermediateFile TrapInclude((
		llvm::sys::Path::GetTemporaryDirectory().str()
		+ llvm::Twine(llvm::sys::PathSeparator) + "trap"
	).str(), "h");
	{
		int Sizes[] = {8, 16, 32, 64};
		Clang.getPreprocessorOpts().Includes.push_back(TrapInclude.str());
		clang::MacroBuilder Builder(TrapInclude.os());
		Builder.defineMacro("__cint__");
		for (size_t i = 0; i < sizeof(Sizes) / sizeof(Sizes[0]); ++i) {
			int Size = Sizes[i];
			const llvm::Twine &Ty = "__INT" + llvm::Twine(Size) + "_TYPE__";
			Builder.append(Ty + " " TRAP_ANNOTATE_PREFIX + llvm::Twine(Size)
				+ "(" + Ty + ", const char *) __attribute__((pure));");
		}
	}
	TrapInclude.os().flush();

	// Output a single, proprocessed file first for rewriting.
	Inputs.clear();
	Inputs.push_back(clang::FrontendInputFile(InFile, clang::IK_C));

	IntermediateFile PPFile(OutputFileStem, "i");
	if (::getenv("DEBUG"))
		PPFile.keep();
	{
		PreprocessAction Act(PPFile.os());
		Clang.ExecuteAction(Act);
	}
	PPFile.os().flush();

	// Drop macros and includes.
	Clang.getPreprocessorOpts().Macros.clear();
	Clang.getPreprocessorOpts().Includes.clear();
	Clang.getPreprocessorOpts().MacroIncludes.clear();

	// Do rewriting.
	Inputs.clear();
	Inputs.push_back(clang::FrontendInputFile(PPFile.str(), clang::IK_PreprocessedC));
	IntermediateFile RewrittenFile(OutputFileStem, "int");
	if (::getenv("DEBUG"))
		RewrittenFile.keep();
	{
		RewriteAction Act(RewrittenFile.os());
		Clang.ExecuteAction(Act);
	}
	RewrittenFile.os().flush();

	// Generate LLVM module.
	Inputs.clear();
	Inputs.push_back(clang::FrontendInputFile(RewrittenFile.str(), clang::IK_PreprocessedC));
	llvm::OwningPtr<llvm::LLVMContext> VMCtx;
	llvm::OwningPtr<llvm::Module> M;
	{
		AnnotateAction Act(clang::Backend_EmitNothing);
		Clang.ExecuteAction(Act);
		VMCtx.reset(Act.takeLLVMContext());
		M.reset(Act.takeModule());
		if (!M)
			return;
	}

	// Generate intrinsic trap calls.
	{
		llvm::PassManager PM;
		PM.add(new llvm::TargetData(M.get()));
		// Strip llvm.dbg.declare first.
		PM.add(llvm::createStripDebugDeclarePass());
		// Lower llvm.ptr.annotate calls to metadata.
		PM.add(createLowerAnnotationPass());
		// From PassManangerBuilder.
		// Don't invoke llvm's reassociate and instcombine
		// since they compeletely rewrite some instructions.
		PM.add(llvm::createTypeBasedAliasAnalysisPass());
		PM.add(llvm::createBasicAliasAnalysisPass());
		PM.add(llvm::createCFGSimplificationPass());
		PM.add(llvm::createScalarReplAggregatesPass());
		PM.add(llvm::createEarlyCSEPass());
		// Sink operations to reduce use-after-check warnings.
		PM.add(llvm::createSinkingPass());
		// Generate llvm.*.with.overflow.* intrinsics.
		PM.add(createOverflowPass());
		// Clean up after overflow.
		PM.add(llvm::createAggressiveDCEPass());
		// Generate trap calls.
		PM.add(createTrapGenPass());
		if (Clang.getCodeGenOpts().CodeModel == "kernel")
			PM.add(createTrapLinuxPass());
		// Verify we are doing well.
		PM.add(llvm::createVerifierPass());
		PM.run(*M);
	}

	// Enable optimization after TrapGen.
	Clang.getCodeGenOpts().DisableLLVMOpts = 0;
	if (SOB == clang::LangOptions::SOB_Defined)
		forceWrap(*M);

	// Run optimizations using command-line options.
	clang::EmitBackendOutput(
		Clang.getDiagnostics(), Clang.getCodeGenOpts(),
		Clang.getTargetOpts(), Clang.getLangOpts(),
		M.get(), clang::Backend_EmitNothing, 0
	);

	// Postprocess.
	{
		llvm::PassManager PM;
		PM.add(new llvm::TargetData(M.get()));
		PM.add(llvm::createTypeBasedAliasAnalysisPass());
		PM.add(llvm::createBasicAliasAnalysisPass());
		// Alias analysis for hoist.
		PM.add(createTrapAliasAnalysisPass());
		// Simplify traps, e.g., pointer subtraction.
		PM.add(createTrapSimplifyPass());
		// Lower overflow intrinsics.
		PM.add(createLowerOverflowPass());
		// Hoist load/GEP instructions for more optimizations.
		PM.add(createHoistPass());
		// Clean up after hoist splits blocks.
		PM.add(llvm::createCFGSimplificationPass());
		PM.add(llvm::createVerifierPass());
		PM.run(*M);
	}

	// Run optimizations again after hoisting.
	clang::EmitBackendOutput(
		Clang.getDiagnostics(), Clang.getCodeGenOpts(),
		Clang.getTargetOpts(), Clang.getLangOpts(),
		M.get(), clang::Backend_EmitNothing, 0
	);

	{
		llvm::PassManager PM;
		PM.add(new llvm::TargetData(M.get()));
		PM.add(llvm::createTypeBasedAliasAnalysisPass());
		PM.add(llvm::createBasicAliasAnalysisPass());
		// Reduce false negaives by moving trap calls out of loops.
		// Put the pass after hoist for more oppurtunities.
		PM.add(createTrapLoopPass());
		PM.run(*M);
	}

	clang::EmitBackendOutput(
		Clang.getDiagnostics(), Clang.getCodeGenOpts(),
		Clang.getTargetOpts(), Clang.getLangOpts(),
		M.get(), clang::Backend_EmitNothing, 0
	);

	// Emit bitcode.
	{
		llvm::raw_fd_ostream *OS = Clang.createDefaultOutputFile(
			true, OutputFileStem, "bc"
		);
		llvm::PassManager PM;
#if 0
		// Set EnableIVRewrite for indvars.
		{
			const char *argv[] = {"cint", "-enable-iv-rewrite", 0};
			llvm::cl::ParseCommandLineOptions(2, const_cast<char **>(argv));
		}
		PM.add(llvm::createIndVarSimplifyPass());
#endif
		// Lower overflow intrinsics again.
		PM.add(createLowerOverflowPass());
		// Ensure each instruction has a name.
		PM.add(llvm::createInstructionNamerPass());
		// Verify before writing to disk.
		PM.add(llvm::createVerifierPass());
		// Write bitcode.
		PM.add(llvm::createBitcodeWriterPass(*OS));
		PM.run(*M);
		OS->flush();
	}
}

void IntAction::forceWrap(llvm::Module &M) {
	for (llvm::Module::iterator mi = M.begin(), me = M.end(); mi != me; ++mi) {
		llvm::Function *F = mi;
		for (llvm::inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
			llvm::BinaryOperator *BO;
			if ((BO = llvm::dyn_cast<llvm::BinaryOperator>(&*i)))
				if (llvm::isa<llvm::OverflowingBinaryOperator>(BO))
					BO->setHasNoSignedWrap(false);
		}
	}
}

static clang::FrontendPluginRegistry::Add<IntAction>
X("int", "Rewrite integer arithmetic");
