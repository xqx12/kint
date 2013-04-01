#pragma once

#include "llvm/Support/ConstantRange.h"

class IntRange : public llvm::ConstantRange {
public:
    IntRange(const llvm::APInt &V) : llvm::ConstantRange(V) {}

    IntRange(const llvm::APInt &Lower, const llvm::APInt &Upper) : llvm::ConstantRange(Lower, Upper) {}

    IntRange(uint32_t BitWidth, bool isFullSet = false) : llvm::ConstantRange(BitWidth, isFullSet)  {}

    IntRange(const llvm::ConstantRange &CR) : llvm::ConstantRange(CR) {}

	IntRange multiply(const IntRange &R) const;

    IntRange sdiv(const IntRange &R) const;

    IntRange urem(const IntRange &R) const;

    IntRange srem(const IntRange &R) const;

	IntRange shl(const IntRange &R) const;

    IntRange ashr(const IntRange &R) const;

	IntRange binaryOr(const IntRange &R) const;

    IntRange binaryXor(const IntRange &R) const;

    IntRange intersectWith(const IntRange &R) const;
};
