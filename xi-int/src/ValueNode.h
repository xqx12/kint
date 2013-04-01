#pragma once
#include "llvm/Function.h"
#include "llvm/Type.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Metadata.h"

#define NAMESTR_LEN 200

struct ValueNode {
    enum ValueKindTy {
        Normal,
        Global
    };
    ValueKindTy Kind;
    union {
        const llvm::Value* V;
        char IDStr[NAMESTR_LEN];
    } U;

    ValueNode(const llvm::Value* V) {
        if (llvm::isa<llvm::StoreInst>(V))
            assert(llvm::dyn_cast<llvm::StoreInst>(V)->getValueOperand()->getType()->isIntegerTy() && "ValueNode must be created for integer type value");
        else if (llvm::isa<llvm::Function>(V))
            assert(llvm::dyn_cast<llvm::Function>(V)->getReturnType()->isIntegerTy() && "ValueNode must be created for integer type value");
        else 
            assert(V->getType()->isIntegerTy() && "ValueNode must be created for integer type value");
        if (llvm::isa<llvm::StoreInst>(V) || llvm::isa<llvm::LoadInst>(V)) {
            const llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V);
            const llvm::MDNode *MD = I->getMetadata("id");
            if (!MD) {
				// Try whether it is a global variable
				llvm::Value *Addr;
				if (llvm::isa<llvm::LoadInst>(I)) 
					Addr = I->getOperand(0);
				else
					Addr = I->getOperand(1);
				char tmp[NAMESTR_LEN];
				strcpy(tmp, "");
				while (llvm::isa<llvm::ConstantExpr>(Addr)) {
					llvm::ConstantExpr *CE = llvm::dyn_cast<llvm::ConstantExpr>(Addr);
					if (CE->getOpcode() == llvm::Instruction::GetElementPtr)
						Addr = CE->getOperand(0);
					else 
						break;
				}
				if (llvm::isa<llvm::GlobalVariable>(Addr))
					if (Addr->hasName())
						strcpy(tmp, Addr->getName().data());
                Kind = Global;
                strcpy(U.IDStr, tmp);
            }
            else {
                Kind = Global;
                const llvm::MDString *MDS = llvm::dyn_cast<llvm::MDString>(MD->getOperand(0));
                strcpy(U.IDStr, MDS->getString().str().c_str());
            }
        }
        else {
            this->Kind = Normal;
            this->U.V = V;
        }
    }

    ValueNode(const ValueNode &A) {
        *this = A;
    }

    ValueNode& operator = (const ValueNode &A) {
        this->Kind = A.Kind;
        if (this->Kind == Global)
            strcpy(this->U.IDStr, A.U.IDStr);
        else
            this->U.V = A.U.V;
        return *this;
    }

    bool operator < (const ValueNode &A) const {
        if (this->Kind < A.Kind) return true;
        if (this->Kind > A.Kind) return false;
        if (this->Kind == Global)
            return strcmp(this->U.IDStr, A.U.IDStr) < 0;
        else
            return this->U.V < A.U.V;
        return true;
    }

    bool operator == (const ValueNode &A) const {
        if (this->Kind != A.Kind) return false;
        if (this->Kind == Global)
           return strcmp(this->U.IDStr, A.U.IDStr) == 0;
        else
            return this->U.V == A.U.V;
        return true;
    }

    bool isUnknown() {
        return (Kind == Global) && (strcmp(U.IDStr, "") == 0);
    }

    void dump() const {
        if (Kind == Global)
            llvm::errs() << "+Global+" << U.IDStr;
        else {
            if (llvm::isa<llvm::Function>(U.V))
                llvm::errs() << "retvalue@" << U.V->getName();
            else if (llvm::isa<llvm::Argument>(U.V)) {
                const llvm::Argument *A = llvm::dyn_cast<llvm::Argument>(U.V);
                llvm::errs() << A->getName() << " (args " << A->getArgNo() << ")@" << A->getParent()->getName();
            }
            else {
                const llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(U.V);
                llvm::errs() << U.V->getName() << "@" << I->getParent()->getParent()->getName();
            }
        }
    }
};

