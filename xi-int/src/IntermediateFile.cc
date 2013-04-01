#include "IntermediateFile.h"
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/ToolOutputFile.h>
#include <iostream>

using std::cerr;

IntermediateFile::IntermediateFile(llvm::StringRef InFile, llvm::StringRef Extension, bool Binary) {
	cerr << "IntermediateFile::IntermediateFile called  \n" ;
	llvm::sys::Path Path(InFile);
	if (InFile == "-")
		Path = "stdin";
	else {
		Path = InFile;
		Path.eraseSuffix();
	}
	Path.appendSuffix(Extension);
	std::string ErrMsg;
	if (Path.makeUnique(true, &ErrMsg))
		llvm::report_fatal_error(ErrMsg);
	Filename = Path.str();
	cerr << "IntermediateFile::Filename =  " << Filename << "\n" ;
	
	Out = new llvm::tool_output_file(
		Filename.c_str(), ErrMsg, (Binary? llvm::raw_fd_ostream::F_Binary: 0)
	);
	if (!ErrMsg.empty())
		llvm::report_fatal_error(ErrMsg);
}

IntermediateFile::~IntermediateFile() {
	delete Out;
}

llvm::raw_fd_ostream &IntermediateFile::os() {
	return Out->os();
}

void IntermediateFile::keep() {
	Out->keep();
}
