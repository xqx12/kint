#pragma once

#include <config.h>
#include <stdint.h>
#include <err.h>
#include <string>

namespace llvm {
	class Value;
}

#if HAVE_BOOLECTOR
#include <limits.h>
extern "C" {
#include <boolector/boolector.h>
}

typedef BtorExp *SMTExpr;

enum {                
	QUERY_UNDEFINED = 0,
	QUERY_SAT,
	QUERY_UNSAT,
	QUERY_TIMEOUT,
	QUERY_FAILED
};

class BoolectorSolver {
public:
	BoolectorSolver() {
		ctx = boolector_new();
		boolector_enable_model_gen(ctx);
		boolector_enable_inc_usage(ctx);
	}

	~BoolectorSolver() {
		assert(boolector_get_refs(ctx) == 0);
		boolector_delete(ctx);
	}

	virtual SMTExpr get(llvm::Value *) = 0;

	template <typename Stream>
	void print(Stream &OS, SMTExpr E, bool smt = 1) {
		FILE *fp = tmpfile();
		if (!fp)
			err(1, "tmpfile");
		if (smt)
			boolector_dump_smt(ctx, fp, E);
		else
			boolector_dump_btor(ctx, fp, E);
		long size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		char *buf = (char *)malloc(size + 1);
		size = fread(buf, 1, size, fp);
		assert(size > 0);
		buf[size] = 0;
		fclose(fp);
		OS << buf;
		free(buf);
	}

	SMTExpr copy(SMTExpr E) {
		return boolector_copy(ctx, E);
	}

	void release(SMTExpr E) {
		boolector_release(ctx, E);
	}

	template <typename Map, typename Stream>
	int query(SMTExpr E, const Map &Decls, Stream &OS) {
		boolector_assume(ctx, E);
		int res;
		switch (boolector_sat(ctx)) {
		default:              res = QUERY_UNDEFINED; break;
		case BOOLECTOR_UNSAT: res = QUERY_UNSAT; break;
		case BOOLECTOR_SAT:
			res = QUERY_SAT;
			// Dump assignments if sat.
			for (typename Map::const_iterator i = Decls.begin(),
			     e = Decls.end(); i != e; ++i) {
				SMTExpr E = i->second;
				if (BTOR_IS_BV_CONST_EXP(E))
					continue;
				char *val = boolector_bv_assignment(ctx, E);
				if (BTOR_IS_BV_VAR_EXP(E))
					OS << boolector_get_symbol_of_var(ctx, E);
				else
					OS << *i->first;
				OS << "\n  " << val << "\n";
				boolector_free_bv_assignment(ctx, val);
			}
			break;
		}
		return res;
	}

	SMTExpr bvconst(unsigned width, uint64_t value) {
		unsigned intbits = sizeof(unsigned) * CHAR_BIT;
		if (width <= intbits)
			return boolector_unsigned_int(ctx, (unsigned)value, width);
		SMTExpr E = boolector_unsigned_int(ctx, (unsigned)value, intbits);
		do {
			value >>= intbits;
			width -= intbits;
			unsigned subw = (width > intbits)? intbits: width;
			SMTExpr hi = boolector_unsigned_int(ctx, (unsigned)value, subw);
			SMTExpr lo = E;
			E = boolector_concat(ctx, hi, lo);
			release(hi);
			release(lo);
		} while (width > intbits);
		return E;
	} 

	SMTExpr bvvar(unsigned width, const char *name) {
		return boolector_var(ctx, width, name);
	}

	unsigned bvwidth(SMTExpr E) {
		return boolector_get_width(ctx, E);
	}

	// Boolector doesn't distinguish bool from bv
	SMTExpr bv2bool(SMTExpr R) { return R; }
	SMTExpr bool2bv(SMTExpr R) { return R; }

	SMTExpr eq(SMTExpr L, SMTExpr R) {
		return boolector_eq(ctx, L, R);
	}

	SMTExpr ne(SMTExpr L, SMTExpr R) {
		return boolector_ne(ctx, L, R);
	}

	SMTExpr ltrue() {
		return boolector_true(ctx);
	}

	SMTExpr lfalse() {
		return boolector_false(ctx);
	}

	SMTExpr lnot(SMTExpr R) {
		return boolector_not(ctx, R);
	}

	SMTExpr land(SMTExpr L, SMTExpr R) { return bvand(L, R); }

	SMTExpr lor(SMTExpr L, SMTExpr R) { return bvor(L, R); }

	SMTExpr ite(SMTExpr Cond, SMTExpr TrueExpr, SMTExpr FalseExpr) {
		return boolector_cond(ctx, Cond, TrueExpr, FalseExpr);
	}

	SMTExpr extract(unsigned high, unsigned low, SMTExpr R) {
		return boolector_slice(ctx, R, high, low);
	}

	SMTExpr zero_extend(unsigned i, SMTExpr R) {
		return boolector_uext(ctx, R, i);
	}

	SMTExpr sign_extend(unsigned i, SMTExpr R) {
		return boolector_sext(ctx, R, i);
	}

	SMTExpr bvredand(SMTExpr R) {
		return boolector_redand(ctx, R);
	}

	SMTExpr bvredor(SMTExpr R) {
		return boolector_redor(ctx, R);
	}

	SMTExpr bvadd(SMTExpr L, SMTExpr R) {
		return boolector_add(ctx, L, R);
	}

	SMTExpr bvsub(SMTExpr L, SMTExpr R) {
		return boolector_sub(ctx, L, R);
	}

	SMTExpr bvmul(SMTExpr L, SMTExpr R) {
		return boolector_mul(ctx, L, R);
	}

	SMTExpr bvsdiv(SMTExpr L, SMTExpr R) {
		return boolector_sdiv(ctx, L, R);
	}

	SMTExpr bvudiv(SMTExpr L, SMTExpr R) {
		return boolector_udiv(ctx, L, R);
	}

	SMTExpr bvsrem(SMTExpr L, SMTExpr R) {
		return boolector_srem(ctx, L, R);
	}

	SMTExpr bvurem(SMTExpr L, SMTExpr R) {
		return boolector_urem(ctx, L, R);
	}

	SMTExpr bvshl(SMTExpr L, SMTExpr R) {
		return shift<boolector_sll>(L, R);
	}

	SMTExpr bvlshr(SMTExpr L, SMTExpr R) {
		return shift<boolector_srl>(L, R);
	}

	SMTExpr bvashr(SMTExpr L, SMTExpr R) {
		return shift<boolector_sra>(L, R);
	}

	SMTExpr bvand(SMTExpr L, SMTExpr R) {
		return boolector_and(ctx, L, R);
	}

	SMTExpr bvor(SMTExpr L, SMTExpr R) {
		return boolector_or(ctx, L, R);
	}

	SMTExpr bvxor(SMTExpr L, SMTExpr R) {
		return boolector_xor(ctx, L, R);
	}

	SMTExpr bvslt(SMTExpr L, SMTExpr R) {
		return boolector_slt(ctx, L, R);
	}

	SMTExpr bvsle(SMTExpr L, SMTExpr R) {
		return boolector_slte(ctx, L, R);
	}

	SMTExpr bvsgt(SMTExpr L, SMTExpr R) {
		return boolector_sgt(ctx, L, R);
	}

	SMTExpr bvsge(SMTExpr L, SMTExpr R) {
		return boolector_sgte(ctx, L, R);
	}

	SMTExpr bvult(SMTExpr L, SMTExpr R) {
		return boolector_ult(ctx, L, R);
	}

	SMTExpr bvule(SMTExpr L, SMTExpr R) {
		return boolector_ulte(ctx, L, R);
	}

	SMTExpr bvugt(SMTExpr L, SMTExpr R) {
		return boolector_ugt(ctx, L, R);
	}

	SMTExpr bvuge(SMTExpr L, SMTExpr R) {
		return boolector_ugte(ctx, L, R);
	}

	// smax/umax for SCEV.

	SMTExpr bvsmax(SMTExpr L, SMTExpr R) {
		SMTExpr Cond = bvsgt(L, R);
		SMTExpr E = ite(Cond, L, R);
		release(Cond);
		return E;
	}

	SMTExpr bvumax(SMTExpr L, SMTExpr R) {
		SMTExpr Cond = bvugt(L, R);
		SMTExpr E = ite(Cond, L, R);
		release(Cond);
		return E;
	}

	// Boolector provides builtin overflow predicates.

	// Boolector doesn't provide a builtin neg overflow check.
	SMTExpr bvneg_overflow(SMTExpr R) {
		SMTExpr zero = bvconst(bvwidth(R), 0);
		SMTExpr E = bvsub_signed_overflow(zero, R);
		release(zero);
		return E;
	}

	SMTExpr bvadd_signed_overflow(SMTExpr L, SMTExpr R) {
		return boolector_saddo(ctx, L, R);
	}

	SMTExpr bvadd_unsigned_overflow(SMTExpr L, SMTExpr R) {
		return boolector_uaddo(ctx, L, R);
	}

	SMTExpr bvsub_signed_overflow(SMTExpr L, SMTExpr R) {
		return boolector_ssubo(ctx, L, R);
	}

	SMTExpr bvsub_unsigned_overflow(SMTExpr L, SMTExpr R) {
		return boolector_usubo(ctx, L, R);
	}

	SMTExpr bvmul_signed_overflow(SMTExpr L, SMTExpr R) {
		return boolector_smulo(ctx, L, R);
	}

	SMTExpr bvmul_unsigned_overflow(SMTExpr L, SMTExpr R) {
		return boolector_umulo(ctx, L, R);
	}

	SMTExpr bvsdiv_overflow(SMTExpr L, SMTExpr R) {
		return boolector_sdivo(ctx, L, R);
	}

private:
	Btor *ctx;

	// The second operand of shift operations must be with bit-width
	// log2 of that of the first.  __builtin_ctz, which gives the number of
	// trailing zeros, is used to do that.
	// NB: The semantics is the same as x86, but different from powerpc.
	// For example, (1 << n) will not yield 0, which may miss bugs like
	// ext4-2009-4307. 
	template <SMTExpr (*F)(Btor *, SMTExpr, SMTExpr)>
	SMTExpr shift(SMTExpr L, SMTExpr R) {
		SMTExpr Log2R = extract(__builtin_ctz(bvwidth(R)) - 1, 0, R);
		SMTExpr E = F(ctx, L, Log2R);
		release(Log2R);
		return E;
	}
};

typedef BoolectorSolver SMTSolver;

#endif // HAVE_STP

#if HAVE_Z3
#include <z3.h>
// Avoid name pollution.
#undef __in
#undef __in_z
#undef __out
#undef __out_z
#undef __ecount
#undef __in_ecount
#undef __out_ecount
#undef __inout_ecount
#undef __inout
#undef Z3_API
#undef DEFINE_TYPE
#undef DEFINE_VOID

typedef Z3_ast SMTExpr;

class Z3Solver {
public:
	Z3Solver() {
		Z3_config cfg = Z3_mk_config();
		// Enable model construction.
		Z3_set_param_value(cfg, "MODEL", "true");
		ctx = Z3_mk_context(cfg);
		Z3_del_config(cfg);
		// Set up constants.
		Z3_sort sort = Z3_mk_bv_sort(ctx, 1);
		bv0 = Z3_mk_int(ctx, 0, sort);
		bv1 = Z3_mk_int(ctx, 1, sort);
	}

	~Z3Solver() { Z3_del_context(ctx); }

	virtual SMTExpr get(llvm::Value *) = 0;

	template <typename Stream>
	void print(Stream &OS, SMTExpr E) {
		OS << Z3_ast_to_string(ctx, E);
	}

	template <typename Map, typename Stream>
	int query(SMTExpr E, const Map &, Stream &OS) {
		/* FIXME: return QUERY_XXXX values */
		Z3_push(ctx);
		Z3_assert_cnstr(ctx, E);
		Z3_model m = 0;
		int res = Z3_check_and_get_model(ctx, &m);
		if (m) {
			OS << Z3_model_to_string(ctx, m) << '\n';
			Z3_del_model(ctx, m);
		}
		Z3_pop(ctx, 1);
		return res;
	}


	// Managed by Z3, no reference counting.
	SMTExpr copy(SMTExpr E) { return E; }
	void release(SMTExpr) { }

	SMTExpr bvconst(unsigned width, uint64_t value) {
		return Z3_mk_int64(ctx, value, Z3_mk_bv_sort(ctx, width));
	} 

	SMTExpr bvvar(unsigned width, const char *name) {
		return Z3_mk_const(ctx, Z3_mk_string_symbol(ctx, name), Z3_mk_bv_sort(ctx, width));
	}

	unsigned bvwidth(SMTExpr E) {
		return Z3_get_bv_sort_size(ctx, Z3_get_sort(ctx, E));
	}

	SMTExpr bv2bool(SMTExpr R) {
		return eq(R, bv1);
	}

	SMTExpr bool2bv(SMTExpr R) {
		return ite(R, bv1, bv0);
	}

	SMTExpr eq(SMTExpr L, SMTExpr R) {
		return Z3_mk_eq(ctx, L, R);
	}

	SMTExpr ne(SMTExpr L, SMTExpr R) {
		return lnot(eq(L, R));
	}

	SMTExpr ltrue() {
		return Z3_mk_true(ctx);
	}

	SMTExpr lfalse() {
		return Z3_mk_false(ctx);
	}

	SMTExpr lnot(SMTExpr R) {
		return Z3_mk_not(ctx, R);
	}

	SMTExpr land(SMTExpr L, SMTExpr R) {
		SMTExpr Children[] = {L, R};
		return Z3_mk_and(ctx, 2, Children);
	}

	SMTExpr lor(SMTExpr L, SMTExpr R) {
		SMTExpr Children[] = {L, R};
		return Z3_mk_or(ctx, 2, Children);
	}

	SMTExpr ite(SMTExpr Cond, SMTExpr TrueExpr, SMTExpr FalseExpr) {
		return Z3_mk_ite(ctx, Cond, TrueExpr, FalseExpr);
	}

	SMTExpr extract(unsigned high, unsigned low, SMTExpr R) {
		return Z3_mk_extract(ctx, high, low, R);
	}

	SMTExpr zero_extend(unsigned i, SMTExpr R) {
		return Z3_mk_zero_ext(ctx, i, R);
	}

	SMTExpr sign_extend(unsigned i, SMTExpr R) {
		return Z3_mk_sign_ext(ctx, i, R);
	}

	SMTExpr bvredand(SMTExpr R) {
		return Z3_mk_bvredand(ctx, R);
	}

	SMTExpr bvredor(SMTExpr R) {
		return Z3_mk_bvredor(ctx, R);
	}

	SMTExpr bvadd(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvadd(ctx, L, R);
	}

	SMTExpr bvsub(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvsub(ctx, L, R);
	}

	SMTExpr bvmul(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvmul(ctx, L, R);
	}

	SMTExpr bvsdiv(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvsdiv(ctx, L, R);
	}

	SMTExpr bvudiv(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvudiv(ctx, L, R);
	}

	SMTExpr bvsrem(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvsrem(ctx, L, R);
	}

	SMTExpr bvurem(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvurem(ctx, L, R);
	}

	SMTExpr bvshl(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvshl(ctx, L, R);
	}

	SMTExpr bvlshr(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvlshr(ctx, L, R);
	}

	SMTExpr bvashr(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvashr(ctx, L, R);
	}

	SMTExpr bvand(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvand(ctx, L, R);
	}

	SMTExpr bvor(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvor(ctx, L, R);
	}

	SMTExpr bvxor(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvxor(ctx, L, R);
	}

	SMTExpr bvslt(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvslt(ctx, L, R);
	}

	SMTExpr bvsle(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvsle(ctx, L, R);
	}

	SMTExpr bvsgt(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvsgt(ctx, L, R);
	}

	SMTExpr bvsge(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvsge(ctx, L, R);
	}

	SMTExpr bvult(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvult(ctx, L, R);
	}

	SMTExpr bvule(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvule(ctx, L, R);
	}

	SMTExpr bvugt(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvugt(ctx, L, R);
	}

	SMTExpr bvuge(SMTExpr L, SMTExpr R) {
		return Z3_mk_bvuge(ctx, L, R);
	}

	// smax/umax for SCEV.

	SMTExpr bvsmax(SMTExpr L, SMTExpr R) {
		return ite(bvsgt(L, R), L, R);
	}

	SMTExpr bvumax(SMTExpr L, SMTExpr R) {
		return ite(bvugt(L, R), L, R);
	}

	// Z3 provides builtin overflow predicates.

	SMTExpr bvneg_overflow(SMTExpr R) {
		return lnot(Z3_mk_bvneg_no_overflow(ctx, R));
	}

	SMTExpr bvadd_signed_overflow(SMTExpr L, SMTExpr R) {
		return lor(
			lnot(Z3_mk_bvadd_no_overflow(ctx, L, R, Z3_TRUE)),
			lnot(Z3_mk_bvadd_no_underflow(ctx, L, R))
		);
	}

	SMTExpr bvadd_unsigned_overflow(SMTExpr L, SMTExpr R) {
		return lnot(Z3_mk_bvadd_no_overflow(ctx, L, R, Z3_FALSE));
	}

	SMTExpr bvsub_signed_overflow(SMTExpr L, SMTExpr R) {
		return lor(
			lnot(Z3_mk_bvsub_no_overflow(ctx, L, R)),
			lnot(Z3_mk_bvsub_no_underflow(ctx, L, R, Z3_TRUE))
		);
	}

	SMTExpr bvsub_unsigned_overflow(SMTExpr L, SMTExpr R) {
		return lnot(Z3_mk_bvsub_no_underflow(ctx, L, R, Z3_FALSE));
	}

	SMTExpr bvmul_signed_overflow(SMTExpr L, SMTExpr R) {
		return lor(
			lnot(Z3_mk_bvmul_no_overflow(ctx, L, R, Z3_TRUE)),
			lnot(Z3_mk_bvmul_no_underflow(ctx, L, R))
		);
	}

	SMTExpr bvmul_unsigned_overflow(SMTExpr L, SMTExpr R) {
		return lnot(Z3_mk_bvmul_no_overflow(ctx, L, R, Z3_FALSE));
	}

	SMTExpr bvsdiv_overflow(SMTExpr L, SMTExpr R) {
		return lnot(Z3_mk_bvsdiv_no_overflow(ctx, L, R));
	}

private:
	Z3_context ctx;
	SMTExpr bv0, bv1;
};

typedef Z3Solver SMTSolver;

#endif // HAVE_Z3
