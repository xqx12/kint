#pragma once
#include "AnnotationManager.h"
#include "ValueNode.h"
#include "IntRange.h"
#include <map>
#include "string.h"
#include "ctype.h"
#include <fstream>
#include "llvm/Type.h"

#define ANNOTATION_LINE_LEN 1000

class VRMap {
    typedef std::map<ValueNode, IntRange> MapTy;
    MapTy Map;
//	const AnnotationManager* AManager;

public:
    VRMap() {
        Map.clear();
//		this->AManager = NULL;
    }

/*	VRMap(const AnnotationManager* AManger) {
		Map.clear();
		this->AManager = AManger;
	}*/

    bool operator == (const VRMap &M) {
        return this->Map == M.Map;
    }

    bool operator != (const VRMap &M) {
        return this->Map != M.Map;
    }

    void clear() {
        Map.clear();
    }

    bool isEmpty() {
        return Map.empty();
    }

    bool setValueRange(llvm::Value *V, IntRange R) {
        bool ret = true;
        ValueNode N(V);
        if (N.isUnknown())
            return false;
        MapTy::iterator it = Map.find(N);
        if (it == Map.end()) 
            Map.insert(std::make_pair(N, R));
        else {
            ret = ! (it->second.contains(R));
            it->second = it->second.unionWith(R);
        }
 
        return ret;
    }

    IntRange getValueRange(llvm::Value *V, bool FullIfNotFound = false) {
        ValueNode N(V);
/*		if (AManager) {
			if (N.Kind == ValueNode::Global) {
				std::pair<long long, long long> p;
				if (AManager->getGlobalRange(N.U.IDStr, p)) {
					llvm::IntegerType *T = llvm::dyn_cast<llvm::IntegerType>(V->getType());
					if (T)
						return IntRange(llvm::APInt(T->getBitWidth(), p.first), llvm::APInt(T->getBitWidth(), p.second));
				}
			}
		}*/
        MapTy::iterator it = Map.find(N);
        if (it == Map.end()) {
			llvm::IntegerType *T;
			if (llvm::isa<llvm::Function>(V))
				T = llvm::dyn_cast<llvm::IntegerType>(llvm::dyn_cast<llvm::Function>(V)->getReturnType());
			else
				T = llvm::dyn_cast<llvm::IntegerType>(V->getType());
            assert(T != 0 && "Request value range of a non-integer type.");
            if (N.isUnknown() || FullIfNotFound)
				return IntRange(T->getBitWidth(), true);
			else
				return IntRange(T->getBitWidth(), false);
        }
        else
            return it->second;
    }

    void unionWith(const VRMap &A) {
        for (MapTy::const_iterator it = A.Map.begin(); it != A.Map.end(); it ++) {
            MapTy::iterator it1 = Map.find(it->first);
            if (it1 != Map.end()) 
                it1->second = it1->second.unionWith(it->second);
			else 
				Map.insert(std::make_pair(it->first, it->second));
        }
    }
	
    void appendConstraints(const VRMap &A) {
        for (MapTy::const_iterator it = A.Map.begin(); it != A.Map.end(); it++) {
            MapTy::iterator it1 = Map.find(it->first);
            if (it1 != Map.end()) 
                it1->second = it1->second.intersectWith(it->second);
        }
    }

	void conditionOr(const VRMap &A) {
		for (MapTy::const_iterator it = A.Map.begin(); it != A.Map.end(); it++) {
			MapTy::iterator it1 = Map.find(it->first);
			if (it1 != Map.end())
				it1->second = it1->second.unionWith(it->second);
		}
		MapTy tmp = Map;
		for (MapTy::iterator it = tmp.begin(); it != tmp.end(); it++) {
			MapTy::const_iterator it1 = A.Map.find(it->first);
			if (it1 == A.Map.end())
				Map.erase(it->first);
		}
	}

	void conditionAnd(const VRMap &A) {
		for (MapTy::const_iterator it = A.Map.begin(); it != A.Map.end(); it++) {
			MapTy::iterator it1 = Map.find(it->first);
			if (it1 != Map.end())
				it1->second = it1->second.intersectWith(it->second);
			else
				Map.insert(std::make_pair(it->first, it->second));
		}
	}

	bool checkAgainst(const VRMap &A) {
		for (MapTy::iterator it = Map.begin(); it != Map.end(); it++) {
			if (! it->second.isFullSet()) {
				MapTy::const_iterator it2 = A.Map.find(it->first);
				if (it2 == A.Map.end())
					return false;
				if (it->second.intersectWith(it2->second).isEmptySet())
					return false;
			}
		}
		return true;
	}

    void dump() {
        MapTy::iterator it;
        for (it = Map.begin(); it != Map.end(); it++) {
            it->first.dump();
            llvm::errs() << ": " << it->second << "\n";
        }
    }
};
