#pragma once
#include <map>
#include <vector>
#include <string>

struct AnnotationExpValue {
	typedef enum {
		ArgumentKind = 0,
		IntKind
	} ExpValueKind;
	ExpValueKind Kind;
	long long D;
};

typedef std::vector<AnnotationExpValue> AnnotationTerm;

struct AnnotationExp {
	std::vector<AnnotationTerm> Terms;
	std::vector<bool> Signs;
};

class AnnotationManager {
	typedef std::map<std::string, std::pair<long long, long long> > GlobalMapTy;
	typedef std::map<std::string, std::pair<AnnotationExp, AnnotationExp> > FuncMapTy;
	GlobalMapTy Global;
	FuncMapTy Func;
public:
	AnnotationManager(const char* FileName);

	bool getGlobalRange(const std::string &Name, std::pair<long long, long long> &R) const {
		GlobalMapTy::const_iterator it = Global.find(Name);
		if (it != Global.end()) {
			R = it->second;
			return true;
		}
		else
			return false;
	}

	bool getFunctionExp(const std::string &Name, std::pair<AnnotationExp, AnnotationExp> &R) const {
		FuncMapTy::const_iterator it = Func.find(Name);
		if (it != Func.end()) {
			R = it->second;
			return true;
		}
		else
			return false;
	}
};
