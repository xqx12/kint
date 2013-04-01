#include "llvm/Value.h"
#include "IntRange.h"
#include <map>

struct VRGraph {
    static char ID;    

    virtual IntRange getValueRange(llvm::Value*) = 0;
};
