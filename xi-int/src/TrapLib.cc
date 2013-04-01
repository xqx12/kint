#include "TrapLib.h"
#include <llvm/Module.h>
#include <llvm/Instructions.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Support/Debug.h>
#include <algorithm>
#include <iostream>

using std::cerr;

namespace cl = llvm::cl;

static cl::list<std::string>
LibAdd("trap-lib", cl::desc("<library>")); 

#pragma mark -
#pragma mark TrapLib

TrapLib::TrapLib(llvm::Module &M) {
	ConstraintRegistry::iterator i = ConstraintRegistry::begin(),
	                             e = ConstraintRegistry::end();
	for (; i != e; ++i) {
		const char *Desc = i->getDesc();
		if (std::find(LibAdd.begin(), LibAdd.end(), Desc) == LibAdd.end())
			continue;
		const char *Name = i->getName();
		llvm::Function *F = M.getFunction(Name);
		if (!F) 
			LoadConstraints[Name] = i->instantiate();
		else
			Constraints[F] = i->instantiate();
	}
}

SMTExpr TrapLib::run(SMTSolver &SMT, llvm::Value *V) {
	llvm::dbgs() << "TrapLib::run "  << "\n";
	//cerr << "TrapLib::run "  << "\n";
	Constraint *C;
	if (llvm::CallInst *CI = llvm::dyn_cast<llvm::CallInst>(V)) {
		llvm::Function *F = CI->getCalledFunction();
		llvm::dbgs() << F << ":" << __LINE__ << "43 \n";
		if (!F || (C = Constraints.lookup(F)) == NULL)
			return 0;
		return C->run(SMT, CI);
	}
	else if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(V)) {
		//llvm::dbgs() << *F <<  __FILE__ << ":" << __LINE__ << "\n";
		//asm("int $3");
		if (!F || (C = Constraints.lookup(F)) == NULL){
			//llvm::dbgs() << *F <<  __FILE__ << ":" << __LINE__ << "\n";
			return 0;
		}
		//llvm::dbgs() << *F <<  __FILE__ << ":" << __LINE__ << "\n";
		return C->run(SMT, F);
	}
	else if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(V)) {
		//llvm::dbgs() << LI << ":" << __LINE__ << " \n";
		llvm::StringRef ID;
		llvm::Value *P = LI->getPointerOperand()->stripPointerCasts();
		if (llvm::isa<llvm::GlobalVariable>(P))
			ID = P->getName();
		else {
			llvm::MDNode *MD = LI->getMetadata("id");
			if (!MD)
				return 0;
			ID = llvm::cast<llvm::MDString>(MD->getOperand(0))->getString();
		}
		if ((C = LoadConstraints.lookup(ID)) != NULL) 
			return C->run(SMT, LI);
	}
	//llvm::dbgs() <<  __FILE__ << ":" << __LINE__ << " \n";
	return 0;
}

#define BEGIN_EXTERN(name, lib) namespace { \
	struct Trap_##name##lib: Constraint { \
		virtual SMTExpr run(SMTSolver &SMT, llvm::Value *V) { \
			return runOnCallSite(SMT, llvm::CallSite(V)); \
		} \
		SMTExpr runOnCallSite(SMTSolver &SMT, llvm::CallSite CS) {

#define END_EXTERN(name, lib) } }; } \
	static ConstraintRegistry::Add<Trap_##name##lib> \
		X__##name(#name, #lib);

//addbyxqx201303
//add a contruct func for print, but no message when run it.
#define BEGIN_FUNC(name, lib) namespace { \
	struct Trap_##name##lib: Constraint { \
	    Trap_##name##lib() { cerr << "test-----\n"; exit(1);};  \
		virtual SMTExpr run(SMTSolver &SMT, llvm::Value *V) { \
			return runOnFunction(SMT, *llvm::cast<llvm::Function>(V)); \
		} \
		SMTExpr runOnFunction(SMTSolver &SMT, llvm::Function &F) {

#define END_FUNC(name, lib) } }; } \
	static ConstraintRegistry::Add<Trap_##name##lib> \
		X__##name(#name, #lib);

#define BEGIN_LOAD(id, name, lib) namespace { \
	struct Trap_##id##lib: Constraint { \
		virtual SMTExpr run(SMTSolver &SMT, llvm::Value *V) { \
			return runOnLoadInst(SMT, *llvm::cast<llvm::LoadInst>(V)); \
		} \
		SMTExpr runOnLoadInst(SMTSolver &SMT, llvm::LoadInst &LI) {

#define END_LOAD(id, name, lib) } }; } \
	static ConstraintRegistry::Add<Trap_##id##lib> \
		X__##id(#name, #lib);

static SMTExpr smin(SMTSolver &SMT, llvm::Value *V, int64_t val) {
	SMTExpr S = SMT.get(V);
	SMTExpr Min = SMT.bvconst(SMT.bvwidth(S), (uint64_t)val);
	SMTExpr E = SMT.bvsge(S, Min);
	SMT.release(Min);
	return E;
}

static SMTExpr smax(SMTSolver &SMT, llvm::Value *V, int64_t val) {
	SMTExpr S = SMT.get(V);
	SMTExpr Max = SMT.bvconst(SMT.bvwidth(S), (uint64_t)val);
	SMTExpr E = SMT.bvsle(S, Max);
	SMT.release(Max);
	return E;
}

static SMTExpr sminmax(SMTSolver &SMT, llvm::Value *V, int64_t min, int64_t max) {
	SMTExpr Lo = smin(SMT, V, min);
	SMTExpr Hi = smax(SMT, V, max);
	SMTExpr E = SMT.land(Lo, Hi);
	SMT.release(Lo);
	SMT.release(Hi);
	return E;
}

#define SMIN(min) return smin(SMT, &LI, min);
#define SMAX(max) return smax(SMT, &LI, max);
#define SMINMAX(min, max) return sminmax(SMT, &LI, min, max);

static SMTExpr umin(SMTSolver &SMT, llvm::Value *V, uint64_t val) {
	SMTExpr S = SMT.get(V);
	SMTExpr Min = SMT.bvconst(SMT.bvwidth(S), val);
	SMTExpr E = SMT.bvuge(S, Min);
	SMT.release(Min);
	return E;
}

static SMTExpr umax(SMTSolver &SMT, llvm::Value *V, uint64_t val) {
	SMTExpr S = SMT.get(V);
	SMTExpr Max = SMT.bvconst(SMT.bvwidth(S), val);
	SMTExpr E = SMT.bvule(S, Max);
	SMT.release(Max);
	return E;
}

static SMTExpr uminmax(SMTSolver &SMT, llvm::Value *V, uint64_t min, uint64_t max) {
	SMTExpr Lo = umin(SMT, V, min);
	SMTExpr Hi = umax(SMT, V, max);
	SMTExpr E = SMT.land(Lo, Hi);
	SMT.release(Lo);
	SMT.release(Hi);
	return E;
}

#define UMIN(min) return umin(SMT, &LI, min);
#define UMAX(max) return umax(SMT, &LI, max);
#define UMINMAX(min, max) return uminmax(SMT, &LI, min, max);

#pragma mark -
#pragma mark Linux

// unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
//                             unsigned long offset);
// if offset >= size:
//    return = size
// else:
//    offset <= return <= size
BEGIN_EXTERN(find_next_bit, linux)
	assert(CS.arg_size() == 3);
	SMTExpr size = SMT.get(CS.getArgument(1));
	SMTExpr offset = SMT.get(CS.getArgument(2));
	SMTExpr ret = SMT.get(CS.getInstruction());
	// if offset >= size
	SMTExpr Cond = SMT.bvuge(offset, size);
	// then return == size
	SMTExpr Unlikely = SMT.eq(ret, size);
	// else return >= offset && return <= size
	SMTExpr Lo = SMT.bvuge(ret, offset), Hi = SMT.bvule(ret, size);
	SMTExpr Likely = SMT.land(Lo, Hi);
	SMTExpr E = SMT.ite(Cond, Unlikely, Likely);
	SMT.release(Cond);
	SMT.release(Unlikely);
	SMT.release(Lo);
	SMT.release(Hi);
	SMT.release(Likely);
	return E;
END_EXTERN(find_next_bit, linux)

// unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size);
// return <= size.
BEGIN_EXTERN(find_first_zero_bit, linux)
	assert(CS.arg_size() == 2);
	SMTExpr size = SMT.get(CS.getArgument(1));
	SMTExpr ret = SMT.get(CS.getInstruction());
	return SMT.bvule(ret, size);
END_EXTERN(find_first_zero_bit, linux)

// int br_fdb_fillbuf(struct net_bridge *br, void *buf,
//                    unsigned long maxnum, unsigned long off);
BEGIN_EXTERN(br_fdb_fillbuf, linux)
	assert(CS.arg_size() == 4);
	SMTExpr maxnum = SMT.get(CS.getArgument(2));
	SMTExpr ret = SMT.get(CS.getInstruction());
	unsigned szret = SMT.bvwidth(ret);
	unsigned szmax = SMT.bvwidth(maxnum);
	SMTExpr E;
	if (szret < szmax) {
		SMTExpr exret = SMT.zero_extend(szmax - szret, ret);
		E = SMT.bvule(exret, maxnum);
		SMT.release(exret);
	} else {
		E = SMT.bvule(ret, maxnum);
	}
	return E;
END_EXTERN(br_fdb_fillbuf, linux)


// i386:
//   unsigned long
//   copy_to_user(void __user *dst, const void *src, unsigned long n);
// x86_64:
//   int copy_to_user(void __user *dst, const void *src, unsigned size);
BEGIN_EXTERN(copy_to_user, linux)
	assert(CS.arg_size() == 3);
	SMTExpr ret = SMT.get(CS.getInstruction());
	SMTExpr cnt = SMT.get(CS.getArgument(2));
	unsigned szret = SMT.bvwidth(ret);
	unsigned szcnt = SMT.bvwidth(cnt);
	SMTExpr E;
	if (szret > szcnt) {
		SMTExpr excnt = SMT.zero_extend(szret - szcnt, cnt);
		E = SMT.bvule(ret, excnt);
		SMT.release(excnt);
	} else {
		E = SMT.bvule(ret, cnt);
	}
	return E;
END_EXTERN(copy_to_user, linux)

// dev >= 0
// void opl3_setup_voice(int dev, int voice, int chn);
BEGIN_FUNC(opl3_setup_voice, linux)
	assert(F.arg_size() == 3);
	SMTExpr dev = SMT.get(F.arg_begin());
	SMTExpr zero = SMT.bvconst(SMT.bvwidth(dev), 0);
	SMTExpr E = SMT.bvsge(dev, zero);
	SMT.release(zero);
	return E;
END_FUNC(opl3_setup_voice, linux)

// Assume that all references to local variable nent > 0.
// This is because everytime do_cpuid_ent() gets called,
// it increases "nent" by at least one.
BEGIN_FUNC(kvm_dev_ioctl_get_supported_cpuid, linux)
	assert(F.arg_size() == 2);
	SMTExpr E = SMT.ltrue();
	llvm::inst_iterator i, e;
	for (i = inst_begin(F), e = inst_end(F); i != e; ++i) {
		if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&*i)) {
			if (LI->getPointerOperand()->getName() == "nent") {
				SMTExpr nent = SMT.get(LI);
				SMTExpr zero = SMT.bvconst(SMT.bvwidth(nent), 0);
				SMTExpr cmp = SMT.bvsgt(nent, zero);
				SMTExpr accu = SMT.land(E, cmp);
				SMT.release(zero);
				SMT.release(cmp);
				SMT.release(E);
				E = accu;
			}
		}
	}
	return E;
END_FUNC(kvm_dev_ioctl_get_supported_cpuid, linux)

BEGIN_LOAD(agp_bridge_max_memory, struct.agp_bridge_data.max_memory_agp, linux)
	SMINMAX(0, 1101568);
END_LOAD(agp_bridge_max_memory, struct.agp_bridge_data.max_memory_agp, linux)

BEGIN_LOAD(agp_bridge_cur_memory, struct.agp_bridge_data.max_memory_agp, linux)
	SMINMAX(0, 1101568);
END_LOAD(agp_bridge_cur_memory, struct.agp_bridge_data.max_memory_agp, linux)

BEGIN_LOAD(struct_drm_device_num_crtcs, struct.drm_device.num_crtcs, linux)
	UMAX(8)
END_LOAD(struct_drm_device_num_crtcs, struct.drm_device.num_crtcs, linux)

BEGIN_LOAD(agp_memory_page_count, struct.agp_memory.page_count, linux)
	UMAX(4)
END_LOAD(agp_memory_page_count, struct.agp_memory.page_count, linux)

BEGIN_LOAD(e1000_rx_ring_count, struct.e1000_rx_ring.count, linux)
	UMINMAX(48, 4096)
END_LOAD(e1000_rx_ring_count, struct.e1000_rx_ring.count, linux)

BEGIN_LOAD(e1000_rx_ring_next_to_clean, struct.e1000_rx_ring.next_to_clean, linux)
	SMTExpr next_to_clean = SMT.get(&LI);
	SMTExpr count = SMT.bvconst(SMT.bvwidth(next_to_clean), 4096);
	SMTExpr E = SMT.bvult(next_to_clean, count);
	SMT.release(count);
	return E;
END_LOAD(e1000_rx_ring_next_to_clean, struct.e1000_rx_ring.next_to_clean, linux)

BEGIN_LOAD(e1000_rx_ring_next_to_use, struct.e1000_rx_ring.next_to_use, linux)
	SMTExpr next_to_use = SMT.get(&LI);
	SMTExpr count = SMT.bvconst(SMT.bvwidth(next_to_use), 4096);
	SMTExpr E = SMT.bvult(next_to_use, count);
	SMT.release(count);
	return E;
END_LOAD(e1000_rx_ring_next_to_use, struct.e1000_rx_ring.next_to_use, linux)

BEGIN_LOAD(e1000_hw_max_frame_size, struct.e1000_hw.max_frame_size, linux)
	SMTExpr max_frame_size = SMT.get(&LI);
	SMTExpr max_jumbo_frame_size = SMT.bvconst(SMT.bvwidth(max_frame_size), 0x3F00);
	SMTExpr min_ethernet_frame_size = SMT.bvconst(SMT.bvwidth(max_frame_size), 64);
	SMTExpr Hi = SMT.bvule(max_frame_size, max_jumbo_frame_size);
	SMTExpr Lo = SMT.bvuge(max_frame_size, min_ethernet_frame_size);
	SMTExpr E = SMT.land(Hi, Lo);
	SMT.release(max_jumbo_frame_size);
	SMT.release(min_ethernet_frame_size);
	SMT.release(Hi);
	SMT.release(Lo);
	return E;
END_LOAD(e1000_hw_max_frame_size, struct.e1000_hw.max_frame_size, linux)

BEGIN_LOAD(e1000_hw_min_frame_size, struct.e1000_hw.min_frame_size, linux)
	SMTExpr min_frame_size = SMT.get(&LI);
	SMTExpr min_ethernet_frame_size = SMT.bvconst(SMT.bvwidth(min_frame_size), 64);
	SMTExpr E = SMT.eq(min_frame_size, min_ethernet_frame_size);
	SMT.release(min_ethernet_frame_size);
	return E;
END_LOAD(e1000_hw_min_frame_size, struct.e1000_hw.min_frame_size, linux)

BEGIN_LOAD(sk_buff_tail, struct.sk_buff.tail, linux)
	SMTExpr tail = SMT.get(&LI);
	SMTExpr zero = SMT.bvconst(SMT.bvwidth(tail), 0);
	SMTExpr E = SMT.bvsgt(tail, zero);
	SMT.release(zero);
	return E;
END_LOAD(sk_buff_tail, struct.sk_buff.tail, linux)

BEGIN_LOAD(super_block_s_blocksize, struct.super_block.s_blocksize, linux)
	SMTExpr s_blocksize = SMT.get(&LI);
	SMTExpr zero = SMT.bvconst(SMT.bvwidth(s_blocksize), 0);
	SMTExpr E = SMT.bvugt(s_blocksize, zero);
	SMT.release(zero);
	return E;
END_LOAD(super_block_s_blocksize, struct.super_block.s_blocksize, linux)

BEGIN_LOAD(opl_devinfo_nr_voice, struct.opl_devinfo.nr_voice, linux)
	UMINMAX(9, 18)
END_LOAD(opl_devinfo_nr_voice, struct.opl_devinfo.nr_voice, linux)

// blocksize \in [1024, 65536], i.e., EXT4_[MIN|MAX]_BLOCK_SIZE
// s_desc_size \in [32, 1024], i.e., EXT4_[MIN|MAX]_DESC_SIZE
// s_desc_per_block = blocksize / s_desc_size, \in [1, 2048]
// s_desc_per_block_bits = log2(s_desc_per_block), \in [0, 11]

BEGIN_LOAD(ext4_sb_info_s_desc_size, struct.ext4_sb_info.s_desc_size, linux)
	UMINMAX(32, 1024)
END_LOAD(ext4_sb_info_s_desc_size, struct.ext4_sb_info.s_desc_size, linux)

BEGIN_LOAD(ext4_sb_info_s_desc_per_block, struct.ext4_sb_info.s_desc_per_block, linux)
	UMINMAX(1, 2048)
END_LOAD(ext4_sb_info_s_desc_per_block, struct.ext4_sb_info.s_desc_per_block, linux)

BEGIN_LOAD(ext4_sb_info_s_desc_per_block_bits, struct.ext4_sb_info.s_desc_per_block_bits, linux)
	UMINMAX(0, 11)
END_LOAD(ext4_sb_info_s_desc_per_block_bits, struct.ext4_sb_info.s_desc_per_block_bits, linux)

BEGIN_LOAD(sysctl_tcp_adv_win_scale, sysctl_tcp_adv_win_scale, linux)
	SMINMAX(-31, 31)
END_LOAD(sysctl_tcp_adv_win_scale, sysctl_tcp_adv_win_scale, linux)

// #define SOCK_MIN_RCVBUF (2048 + sizeof(struct sk_buff))
BEGIN_LOAD(sock_sk_rcvbuf, struct.sock.sk_rcvbuf, linux)
	SMIN(2048)
END_LOAD(sock_sk_rcvbuf, struct.sock.sk_rcvbuf, linux)

// /proc/pid/oom_score_adj ranges from -1000 to +1000
BEGIN_LOAD(signal_struct_oom_score_adj, struct.signal_struct.oom_score_adj, linux)
	SMINMAX(-1000, 1000)
END_LOAD(signal_struct_oom_score_adj, struct.signal_struct.oom_score_adj, linux)

#pragma mark -
#pragma mark libc

BEGIN_FUNC(main, libc)
	assert(F.arg_size() > 1);
	return uminmax(SMT, F.arg_begin(), 1, 1024);
END_FUNC(main, libc)

BEGIN_LOAD(optind, rpl_optind, libc)
	UMINMAX(1, 1024)
END_LOAD(optind, rpl_optind, libc)
