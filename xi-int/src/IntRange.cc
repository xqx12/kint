#include "IntRange.h"
#include "llvm/Support/raw_ostream.h"

namespace {

int divHelper(const IntRange &R, llvm::APInt &l, llvm::APInt &r) {
	cerr << "divHelper called\n" ;
    l = R.getSignedMin(); r = R.getSignedMax();
    if (l.isNegative() && r.isStrictlyPositive())
        return 0;
    if (l.isNegative()) {
        llvm::APInt tmp = r;
        r = -l;
        l = -tmp;
        return -1;
    }
    else
        return 1;
}

} // anonymous namespace

IntRange IntRange::multiply(const IntRange &R) const {
	cerr << "IntRange::multiply called\n" ;
	if (isFullSet() || R.isFullSet())
		return IntRange(getBitWidth(), true);

	if (isWrappedSet() || R.isWrappedSet()) {
		llvm::APInt thisMin = getSignedMin().sext(getBitWidth() * 2);
		llvm::APInt thisMax = getSignedMax().sext(getBitWidth() * 2);
		llvm::APInt otherMin = R.getSignedMin().sext(getBitWidth() * 2);
		llvm::APInt otherMax = R.getSignedMax().sext(getBitWidth() * 2);
		llvm::APInt V1 = thisMin * otherMin;
		llvm::APInt V2 = thisMin * otherMax;
		llvm::APInt V3 = thisMax * otherMin;
		llvm::APInt V4 = thisMax * otherMax;
		llvm::APInt rMin = llvm::APIntOps::smin(llvm::APIntOps::smin(V1, V2), llvm::APIntOps::smin(V3, V4));
		llvm::APInt rMax = llvm::APIntOps::smax(llvm::APIntOps::smax(V1, V2), llvm::APIntOps::smax(V3, V4));
		return IntRange(rMin, rMax + 1).truncate(getBitWidth());
	}
	else
		return llvm::ConstantRange::multiply(R);
}

IntRange IntRange::sdiv(const IntRange &R) const {
	cerr << "IntRange::sdiv called\n" ;
    llvm::APInt l1, r1, l2, r2;
    int s1, s2;
	if (isEmptySet() || R.isEmptySet())
		return IntRange(getBitWidth(), false);

    s1 = divHelper(*this, l1, r1);
    s2 = divHelper(R, l2, r2);
    // If there are more than one sign in any oprand, return FullSet
    if (s1 != 0 && s2 != 0) {
		IntRange tmp = IntRange(l1, r1 + 1).udiv(IntRange(l2, r2 + 1));
		if (s1 * s2 == -1)
			tmp = IntRange(llvm::APInt(getBitWidth(), 0)).sub(tmp);
	    return tmp;
	}
	else if (s1 == 0 && s2 != 0) {
		if (l2 == 0)
			l2 = l2 + 1;
		llvm::APInt min, max;
		if (s2 == 1) {
			min = l1.sdiv(l2);
			max = r1.sdiv(l2);
		}
		else {
			min = -r1.sdiv(l2);
			max = -l1.sdiv(l2);
		}
		if (min == max + 1)
			return IntRange(getBitWidth(), true);
		else
			return IntRange(min, max + 1);
	}
	else {
		llvm::APInt um = llvm::APIntOps::umax(l1.abs(), r1.abs());
		if (um.countLeadingZeros() == 0)
			return IntRange(getBitWidth(), true);
		return IntRange(-um, um+1);
	}
	assert(0);
}


IntRange IntRange::urem(const IntRange &R) const {
	cerr << "IntRange::urem called\n" ;
    if (isEmptySet() || R.isEmptySet())
        return IntRange(getBitWidth(), false);

    llvm::APInt umax = R.getUnsignedMax();
    return IntRange(llvm::APInt(getBitWidth(), 0), umax);
}

IntRange IntRange::srem(const IntRange &R) const {
    if (isEmptySet() || R.isEmptySet())
        return IntRange(getBitWidth(), false);

    llvm::APInt s = llvm::APIntOps::umax(R.getSignedMax().abs(), R.getSignedMin().abs());
    if (s == 0)
        return IntRange(getBitWidth(), false);
    llvm::APInt smin = getSignedMin();
    llvm::APInt smax = getSignedMax();

    if (! smax.isStrictlyPositive())
        return IntRange(-s + 1, llvm::APInt(getBitWidth(), 1));
    else if (smin.isNonNegative())
        return IntRange(llvm::APInt(getBitWidth(), 0), s);
    else {
        if (s.getActiveBits() != getBitWidth())
            return IntRange(-s + 1, s);
        else
            return IntRange(getBitWidth(), true);
    }
}

IntRange IntRange::shl(const IntRange &R) const {
	cerr << "IntRange::shl called\n" ;
	if (isEmptySet() || R.isEmptySet())
		return IntRange(getBitWidth(), false);

	llvm::APInt smin = getSignedMin();
	llvm::APInt smax = getSignedMax();
	unsigned long long tmp = R.getUnsignedMax().getZExtValue();
	if (tmp > getBitWidth())
		return IntRange(getBitWidth(), true);
	if (((smin.countLeadingZeros() <= tmp) && (smin.countLeadingZeros() > 0)) || ((smin.countLeadingOnes() <= tmp) && (smin.countLeadingOnes() > 0))) 
		return IntRange(getBitWidth(), true);
	if (((smax.countLeadingZeros() <= tmp) && (smax.countLeadingZeros() > 0)) || ((smax.countLeadingOnes() <= tmp) && (smax.countLeadingOnes() > 0)))  
		return IntRange(getBitWidth(), true);
	return IntRange(smin.shl(R.getUnsignedMax()), smax.shl(R.getUnsignedMax()) + 1);
}

IntRange IntRange::ashr(const IntRange &R) const {
	cerr << "IntRange::ashr called\n" ;
    if (isEmptySet() || R.isEmptySet())
        return IntRange(getBitWidth(), false);
    
    llvm::APInt sMax, sMin, max, min;
    sMax = getSignedMax();
    sMin = getSignedMin();
    if (sMax.isNegative())
        max = sMax.ashr(R.getUnsignedMax());
    else
        max = sMax.ashr(R.getUnsignedMin());
    if (sMin.isNegative())
        min = sMin.ashr(R.getUnsignedMin());
    else
        min = sMin.ashr(R.getUnsignedMax());

    if (min == max + 1)
        return IntRange(getBitWidth(), true);

    return IntRange(min, max + 1);
}

IntRange IntRange::binaryOr(const IntRange &R) const {
	cerr << "IntRange::binaryOr called\n" ;
	if (isSingleElement()) {
		llvm::errs() << *this << " " << R << "\n";
		if (R.isEmptySet())
			return IntRange(getBitWidth(), false);
		llvm::APInt ele = *getSingleElement();
		llvm::APInt umin = R.getUnsignedMin();
		llvm::APInt umax = R.getUnsignedMax();
		if ((ele | umax) == (ele + umax))
			return IntRange(llvm::APIntOps::umin(ele, umin), (ele | umax) + 1);
		else
			return llvm::ConstantRange::binaryOr(R);
	}
	else if (R.isSingleElement())
		return R.binaryOr(*this);
	else
		return llvm::ConstantRange::binaryOr(R);
}

IntRange IntRange::binaryXor(const IntRange &R) const {
	cerr << "IntRange::binaryXor called\n" ;
    if (isEmptySet() || R.isEmptySet())
        return IntRange(getBitWidth(), false);
    
    llvm::APInt umax1 = getUnsignedMax();
    llvm::APInt umax2 = R.getUnsignedMax();
    size_t s = std::max(umax1.getActiveBits(), umax2.getActiveBits());
    if (s == getBitWidth())
        return IntRange(getBitWidth(), true);
    else
        return IntRange(llvm::APInt(getBitWidth(), 0), llvm::APInt::getAllOnesValue(s).zext(getBitWidth()) + 1);
}

/// intersectWith - Return the range that results from the intersection of this
/// range with another range.  The resultant range is guaranteed to include all
/// elements contained in both input ranges, and to have the smallest possible
/// set size that does so.  Because there may be two intersections with the
/// same set size, A.intersectWith(B) might not be equal to B.intersectWith(A).
IntRange IntRange::intersectWith(const IntRange &CR) const {
	cerr << "IntRange::intersectWith called\n" ;
  assert(getBitWidth() == CR.getBitWidth() && 
         "ConstantRange types don't agree!");

  // Handle common cases.
  
  if (   isEmptySet() || CR.isFullSet()) return *this;
  if (CR.isEmptySet() ||    isFullSet()) return CR;

  if (!isWrappedSet() && CR.isWrappedSet())
    return CR.intersectWith(*this);

  llvm::APInt Lower = getLower();
  llvm::APInt Upper = getUpper();
  
  if (!isWrappedSet() && !CR.isWrappedSet()) {
    if (Lower.ult(CR.getLower())) {
      if (Upper.ule(CR.getLower()))
        return IntRange(getBitWidth(), false);

      if (Upper.ult(CR.getUpper()))
        return IntRange(CR.getLower(), Upper);

      return CR;
    } else {
      if (Upper.ult(CR.getUpper()))
        return *this;

      if (Lower.ult(CR.getUpper()))
        return IntRange(Lower, CR.getUpper());

      return IntRange(getBitWidth(), false);
    }
  }

  if (isWrappedSet() && !CR.isWrappedSet()) {
    if (CR.getLower().ult(Upper)) {
      if (CR.getUpper().ult(Upper))
        return CR;

      if (CR.getUpper().ule(Lower)) // This line was buggy in LLVM
        return IntRange(CR.getLower(), Upper);

      if (getSetSize().ult(CR.getSetSize()))
        return *this;
      else
        return CR;
    } else if (CR.getLower().ult(Lower)) {
      if (CR.getUpper().ule(Lower))
        return IntRange(getBitWidth(), false);

      return IntRange(Lower, CR.getUpper());
    }
    return CR;
  }

  if (CR.getUpper().ult(Upper)) {
    if (CR.getLower().ult(Upper)) {
      if (getSetSize().ult(CR.getSetSize()))
        return *this;
      else
        return CR;
    }

    if (CR.getLower().ult(Lower))
      return IntRange(Lower, CR.getUpper());

    return CR;
  } else if (CR.getUpper().ult(Lower)) {
    if (CR.getLower().ult(Lower))
      return *this;

    return IntRange(CR.getLower(), Upper);
  }
  if (getSetSize().ult(CR.getSetSize()))
    return *this;
  else
    return CR;
}

