//===- llvm-link.cpp - Low-level LLVM linker ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-link a.bc b.bc c.bc -o x.bc
//
//===----------------------------------------------------------------------===//

#include "llvm/LLVMContext.h"
#include "llvm/PassManager.h"
#include "llvm/Module.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/IRReader.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Path.h"
#include <memory>

#include "CallInfo.h"
#include "TaintInfo.h"
#include "RangeInfo.h"
#include <iostream>

using std::cerr;

static llvm::cl::list<std::string>
InputFilenames(llvm::cl::Positional, llvm::cl::OneOrMore,
               llvm::cl::desc("<input bitcode files>"));

static llvm::cl::opt<bool>
Verbose("v", llvm::cl::desc("Print information about actions taken"));

static llvm::cl::opt<std::string>
OutputFilename("o", llvm::cl::desc("Override output filename"), 
			   llvm::cl::init("-"),
			   llvm::cl::value_desc("filename"));

std::vector<llvm::Module *> Modules;
LinkerContext LinkerCtx;


void doCallInfo(llvm::raw_ostream &OS)
{
	cerr << "doCallInfo called\n" ;
	CallInfo info(&LinkerCtx);
	
	if (Verbose)
		llvm::errs() << "\nCollecting global functions and "
			"function pointer initializers...\n";
	
	std::vector<llvm::Module *>::iterator i, e;
	for (i = Modules.begin(), e = Modules.end(); i != e; ++i) {
		info.collectGlobalFunctions(*i);
		info.collectStructs(*i);
		info.collectFPInitializers(*i);
	}

	if (Verbose)
		llvm::errs() << "\nCollecting function pointer assignements ...\n";

	unsigned iter = 0, changed = 1;
	while (changed) {
		++iter;
		changed = 0;
		if (Verbose)
			llvm::errs() << "\n***** Iteration " << iter << " *****\n";
		for (i = Modules.begin(), e = Modules.end(); i != e; ++i) {
			if (Verbose)
				llvm::errs() << "[CallInfo / " << iter << "] '" << 
				(*i)->getModuleIdentifier() << "'";
			bool ret = info.collectFPAssignments(*i);
			if (ret) ++changed;
			if (Verbose)
				llvm::errs() << (ret ? " [CHANGED]" : "") << "\n";
		}
		if (Verbose)
			llvm::errs() << "Updated in " << changed << " files.\n";
	}
	
	// LinkerCtx.dumpFuncPtrs(OS);
	
	if (Verbose)
		llvm::errs() << "\nCollecting callers / callees...\n";
	
	for (i = Modules.begin(), e = Modules.end(); i != e; ++i)
		info.collectCallers(*i);
	
	// LinkerCtx.dumpCallers(OS);
}

void doTaintInfo(llvm::raw_ostream &OS) 
{
	TaintInfo info(&LinkerCtx);
	std::vector<llvm::Module *>::iterator i, e;

	if (Verbose)
		llvm::errs() << "\nInter-procedure taint propagation ...\n";
	
	unsigned iter = 0, changed = 1;
	while (changed) {
		++iter;
		changed = 0;
		if (Verbose)
			llvm::errs() << "\n***** Iteration " << iter << " *****\n";
		for (i = Modules.begin(), e = Modules.end(); i != e; ++i) {
			if (Verbose)
				llvm::errs() << "[Taint / " << iter << "] '" << 
				(*i)->getModuleIdentifier() << "'";
			bool ret = info.updateTaint(*i);
			if (ret) ++changed;
			if (Verbose)
				llvm::errs() << (ret ? " [CHANGED]" : "") << "\n";
		}
		if (Verbose)
			llvm::errs() << "Updated in " << changed << " files.\n";
	}
	
	// Output
	LinkerCtx.outputTaints(OS);
}

void doRangeInfo(llvm::raw_ostream &OS)
{
	RangeInfo info(&LinkerCtx);
	
	if (Verbose)
		llvm::errs() << "\nRange analysis ...\n";

	std::vector<llvm::Module *>::iterator i, e;
	for (i = Modules.begin(), e = Modules.end(); i != e; ++i) {
		if (Verbose)
			llvm::errs() << "Processing '" << 
				(*i)->getModuleIdentifier() << "'\n";
		info.collectInitializers(*i);
	}
	
	unsigned iter = 0, changed = 1, lastChanged = UINT_MAX;
	while (changed) {
		++iter;
		changed = 0;
		if (Verbose)
			llvm::errs() << "\n***** Iteration " << iter << " *****\n";
		for (i = Modules.begin(), e = Modules.end(); i != e; ++i) {
			if (Verbose)
				llvm::errs() << "[Range / " << iter << "] '" << 
				(*i)->getModuleIdentifier() << "'";
			bool ret = info.updateRangeFor(*i);
			if (ret) ++changed;
			if (Verbose)
				llvm::errs() << (ret ? " [CHANGED]" : "") << "\n";
		}
		if (Verbose)
			llvm::errs() << "Updated in " << changed << " files.\n";
		
		// stop if we don't make any progress
		if (changed == lastChanged || iter > 12)
			break;
		lastChanged = changed;
	}
	
	LinkerCtx.outputRanges(OS);

}

int main(int argc, char **argv)
{
	// Print a stack trace if we signal out.
	llvm::sys::PrintStackTraceOnErrorSignal();
	llvm::PrettyStackTraceProgram X(argc, argv);

	llvm::LLVMContext &Context = llvm::getGlobalContext();
	llvm::llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
	llvm::cl::ParseCommandLineOptions(argc, argv, "cint linker\n", true);

	llvm::SMDiagnostic Err;	

	
	/* Open output file */
	std::string ErrorInfo;
	llvm::tool_output_file Out(OutputFilename.c_str(), ErrorInfo);
	if (!ErrorInfo.empty()) {
		llvm::errs() << ErrorInfo << '\n';
		return 1;
	}
	
	/* Loading modules */
	if (Verbose)
		llvm::errs() << "Total " << InputFilenames.size() << " file(s)\n";
	for (unsigned i = 0; i < InputFilenames.size(); ++i) {
		llvm::Module *M = ParseIRFile(InputFilenames[i], Err, Context);

		if (M == NULL) {
			llvm::errs() << argv[0] << ": error loading file '" 
				<< InputFilenames[i] << "'\n";
			continue;
		}

		if (Verbose)
			llvm::errs() << "Loading '" << InputFilenames[i] << "'\n";
		Modules.push_back(M);
	}
	
	/* Main workflow */
	doCallInfo(Out.os());
	doTaintInfo(Out.os());
	doRangeInfo(Out.os());

	/* Write output */
	if (Verbose)
		llvm::errs() << "Writing result ...\n";

	Out.os().flush();
	Out.keep();

	return 0;
}
