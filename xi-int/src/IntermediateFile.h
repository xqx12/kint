#pragma once

#include <llvm/include/llvm/ADT/StringRef.h>

namespace llvm {
	class raw_fd_ostream;
	class tool_output_file;
} // namespace llvm

// Intermediate file to be removed.
class IntermediateFile {
public:
	IntermediateFile(llvm::StringRef InFile, llvm::StringRef Extension, bool Binary = false);
	~IntermediateFile();

	const std::string &str() const { return Filename; }

	llvm::raw_fd_ostream &os();

	void keep();

private:
	std::string Filename;
	llvm::tool_output_file *Out;
};
