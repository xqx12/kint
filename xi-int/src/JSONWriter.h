#pragma once

#include <llvm/Support/raw_ostream.h>

template <typename ListTy, typename DictTy = typename ListTy::value_type>
class JSONWriter {
public:
	static const size_t Width = 2;

	JSONWriter(llvm::raw_ostream &OS, unsigned Indent = 0)
		: OS(OS), Indent(0) { }

	JSONWriter &operator <<(const llvm::StringRef &Str) {
		return writeString(Str);
	}

	JSONWriter &operator <<(const char *Str) {
		return writeString(Str);
	}

	JSONWriter &operator <<(const std::string &Str) {
		return writeString(Str);
	}

	JSONWriter &operator <<(const ListTy &List) {
		typedef typename ListTy::const_iterator iterator;
		bool IsFirst = true;
		OS << '[';
		Indent += Width;
		for (iterator i = List.begin(), e = List.end(); i != e; ++i) {
			if (IsFirst)
				IsFirst = false;
			else
				OS << ',';
			OS << '\n';
			OS.indent(Indent);
			*this << *i;
		}
		OS << '\n';
		Indent -= Width;
		OS.indent(Indent);
		OS << ']';
		return *this; 
	}

	JSONWriter &operator <<(const DictTy &Dict) {
		typedef typename DictTy::const_iterator iterator;
		bool IsFirst = true;
		OS << '{';
		Indent += Width;
		for (iterator i = Dict.begin(), e = Dict.end(); i != e; ++i) {
			if (IsFirst)
				IsFirst = false;
			else
				OS << ',';
			OS << '\n';
			OS.indent(Indent);
			*this << i->first;
			OS << ": ";
			*this << i->second;
		}
		OS << '\n';
		Indent -= Width;
		OS.indent(Indent);
		OS << '}';
		return *this; 
	}

private:
	llvm::raw_ostream &OS;
	unsigned Indent;

	JSONWriter &writeString(llvm::StringRef Str) {
		std::string s(Str);
		for (size_t i = 0; i != s.size(); ++i) {
			const char *escaped;
			switch (s[i]) {
			default: escaped = (isprint(s[i]))? 0: ""; break;
			case '\n': escaped = "\\n"; break;
			case '\"': escaped = "\\\""; break;
			case '\\': escaped = "\\\\";   break;
			}
			if (escaped) {
				s.replace(i, 1, escaped);
				i += (strlen(escaped) - 1);
			}
		}
		OS << '"' << s << '"';
		return *this;
	}
};
