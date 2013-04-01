#include "ValueNode.h"
#include <map>
#include <set>
#include <vector>
#include <queue>
#include <llvm/Module.h>

class RangeWorkList {
    typedef std::vector<llvm::Value*> PropagationListTy;
    typedef std::map<ValueNode, PropagationListTy> PropagationGraphTy;
    typedef std::map<llvm::BasicBlock*, PropagationListTy> BBPropagationGraphTy;
    PropagationGraphTy PGraph;
    BBPropagationGraphTy BBPGraph;

    std::queue<llvm::Value*> WL;
    std::set<llvm::Value*> Open;

    void tryEnque(llvm::Value *V) {
        if (Open.count(V)) return;
        WL.push(V);
        Open.insert(V);
    }

public:
    RangeWorkList(llvm::Module &M) {
        PGraph.clear();
        BBPGraph.clear();
        Open.clear();
        for (llvm::Module::iterator fit = M.begin(); fit != M.end(); fit ++) {
            llvm::Function *F = &*fit;
            for (llvm::Function::iterator bit = F->begin(); bit != F->end(); bit ++) {
                llvm::BasicBlock *BB = &*bit;
                tryEnque(BB);
                llvm::TerminatorInst *TI = BB->getTerminator();

                //TODO: more terminator case
                std::vector<llvm::Value*> tmp;
                tmp.clear();
                if (llvm::isa<llvm::BranchInst>(TI)) {
                    llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(TI);
                    if (BI->isConditional()) {
                        llvm::CmpInst *CI = llvm::dyn_cast<llvm::CmpInst>(BI->getCondition());
                        if (CI) {
                            llvm::Value *op1 = CI->getOperand(0);
                            llvm::Value *op2 = CI->getOperand(1);
                            if (!llvm::isa<llvm::Constant>(op1) && op1->getType()->isIntegerTy())
                                tmp.push_back(op1);
                            if (!llvm::isa<llvm::Constant>(op2) && op2->getType()->isIntegerTy())
                                tmp.push_back(op2);
                        }
                    }
                }
                else if (llvm::isa<llvm::SwitchInst>(TI)) {
                    llvm::SwitchInst *SI = llvm::dyn_cast<llvm::SwitchInst>(TI);
                    llvm::Value *V = SI->getCondition();
                    if (!llvm::isa<llvm::Constant>(V) && V->getType()->isIntegerTy())
                        tmp.push_back(V);
                }

                for (size_t i = 0; i < TI->getNumSuccessors(); i++) {
                    llvm::BasicBlock *SuccBB = TI->getSuccessor(i);
                    BBPGraph[BB].push_back(SuccBB);
                    for (size_t j = 0; j < tmp.size(); j++)
                        PGraph[ValueNode(tmp[j])].push_back(SuccBB);
                    for (llvm::BasicBlock::iterator iit = SuccBB->begin(); iit != SuccBB->end(); iit ++) {
                        llvm::Instruction *I = &*iit;
                        llvm::PHINode *PI = llvm::dyn_cast<llvm::PHINode>(I);
                        if (PI && PI->getType()->isIntegerTy()) {
                            BBPGraph[BB].push_back(PI);
                            for (size_t j = 0; j < tmp.size(); j++)
                                PGraph[ValueNode(tmp[j])].push_back(PI);
                        }
                    }
                }

                for (llvm::BasicBlock::iterator iit = BB->begin(); iit != BB->end(); iit ++) {
                    llvm::Instruction *I = &*iit;
                    if (I->getType()->isIntegerTy() || llvm::isa<llvm::CallInst>(I) || llvm::isa<llvm::StoreInst>(I) || llvm::isa<llvm::TerminatorInst>(I)) {
                        tryEnque(I);
                        BBPGraph[BB].push_back(I);
                        if (llvm::isa<llvm::CallInst>(I)) {
                            llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(I);
                            //TODO: function pointer
                            llvm::Function *F = CI->getCalledFunction();
                            if (!F) continue;
                            if (F->isDeclaration()) continue;
							if (!F->getReturnType()->isIntegerTy()) continue;
                            PGraph[ValueNode(F)].push_back(I);
                        }

                        for (size_t i = 0; i < I->getNumOperands(); i++) {
                            llvm::Value *V = I->getOperand(i);
                            if (V->getType()->isIntegerTy())
                                PGraph[ValueNode(V)].push_back(I);
                        }
                    }
                }
            }
        }
    }

    llvm::Value* deque() {
        assert(! isEmpty() && "Cannot deque empty worklist.");
        llvm::Value *V = WL.front();
        WL.pop();
        Open.erase(V);
        return V;
    }
    
    void update(llvm::Value* V) {
        if (llvm::isa<llvm::BasicBlock>(V)) {
            llvm::BasicBlock *BB = llvm::dyn_cast<llvm::BasicBlock>(V);
            PropagationListTy &P = BBPGraph[BB];
            for (size_t i = 0; i < P.size(); i++)
                tryEnque(P[i]);
        }
        else {
            PropagationListTy &P = PGraph[ValueNode(V)];
            for (size_t i = 0; i < P.size(); i++)
                tryEnque(P[i]);
        }
    }

    bool isEmpty() {
        return WL.empty();
    }
};
