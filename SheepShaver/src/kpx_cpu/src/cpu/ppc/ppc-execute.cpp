/*
 *  ppc-execute.cpp - PowerPC semantics
 *
 *  Kheperix (C) 2003-2005 Gwenole Beauchesne
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#ifdef __MINGW64__
#include <fenv.h>
#endif
#include "cpu/vm.hpp"
#include "cpu/ppc/ppc-cpu.hpp"
#include "cpu/ppc/ppc-bitfields.hpp"
#include "cpu/ppc/ppc-operands.hpp"
#include "cpu/ppc/ppc-operations.hpp"
#include "cpu/ppc/ppc-execute.hpp"

#ifndef SHEEPSHAVER
#include "basic-kernel.hpp"
#endif

#ifdef SHEEPSHAVER
#include "main.h"
#include "prefs.h"
#include "cpu_emulation.h"
#endif

#if ENABLE_MON
#include "mon.h"
#include "mon_disass.h"
#endif

#define DEBUG 0
#include "debug.h"

// --------------------------------------------------------------
// Write-site watchpoint state.
//
// Purpose: capture every 4-byte store of the literal value
// 0x50010000 to a guest-RAM address. Dedup by (PC, target_addr)
// tuple, capped at 32 unique tuples. State is emitted by
// execute_illegal's watchpoint summary block at SIGILL time.
//
// Why this matters: the earlier scan passes ruled out the bad TVECT being
// resident in any scanned region at SIGILL time. The watchpoint
// catches transient writes that get overwritten before the crash.
// Linkage: declared extern in execute_illegal's summary block.
// --------------------------------------------------------------
#ifdef SHEEPSHAVER
extern uint32 RAMBase;
extern uint32 RAMSize;
extern uint32 ROMBase;

static const uint32 kCycle14WpValue = 0x50010000;
static const int    kCycle14WpCap   = 32;
int      cycle14_wp_count    = 0;
int      cycle14_wp_overflow = 0;
uint32   cycle14_wp_pcs[kCycle14WpCap]      = {0};
uint32   cycle14_wp_lrs[kCycle14WpCap]      = {0};
uint32   cycle14_wp_eas[kCycle14WpCap]      = {0};
uint32   cycle14_wp_seen_at[kCycle14WpCap]  = {0};
static uint32 cycle14_wp_block_counter = 0;
static bool   cycle14_wp_summary_emitted = false;

// One-shot inline check. Hot-path safe: a single cheap value
// compare gates everything. Only runs the dedup loop on a
// 0x50010000 store, which is rare in normal execution.
static inline void cycle14_watchpoint(uint32 cur_pc, uint32 cur_lr,
                                      uint32 ea, uint32 value)
{
	if (__builtin_expect(value != kCycle14WpValue, 1)) return;
	// Range gate: only log writes to guest-mapped regions (RAM +
	// the standard SheepMem [0x60000000, 0x60080000) reservation +
	// ROM, although stores into ROM are unusual).
	bool in_ram = (RAMBase != 0 && ea >= RAMBase && ea < (RAMBase + RAMSize));
	bool in_sheep = (ea >= 0x60000000u && ea < 0x60080000u);
	bool in_rom = (ROMBase != 0 && ea >= ROMBase && ea < (ROMBase + 0x400000u));
	if (!in_ram && !in_sheep && !in_rom) return;

	cycle14_wp_block_counter++;

	// Dedup by (PC, addr) tuple. Linear scan over a tiny static array.
	for (int i = 0; i < cycle14_wp_count; i++) {
		if (cycle14_wp_pcs[i] == cur_pc && cycle14_wp_eas[i] == ea) {
			return;
		}
	}
	if (cycle14_wp_count >= kCycle14WpCap) {
		cycle14_wp_overflow++;
		return;
	}
	cycle14_wp_pcs[cycle14_wp_count]     = cur_pc;
	cycle14_wp_lrs[cycle14_wp_count]     = cur_lr;
	cycle14_wp_eas[cycle14_wp_count]     = ea;
	cycle14_wp_seen_at[cycle14_wp_count] = cycle14_wp_block_counter;
	cycle14_wp_count++;
	// Eager log: useful even if SIGILL never fires (e.g. crash
	// kills the host before execute_illegal runs).
	fprintf(stderr, "[watchpoint] PC=0x%08x LR=0x%08x  stw value=0x%08x -> target=0x%08x  (unique tuple #%d, region=%s)\n",
			cur_pc, cur_lr, value, ea, cycle14_wp_count - 1,
			in_ram ? "RAM" : (in_sheep ? "SheepMem" : "ROM"));
}
#endif

/**
 *	Illegal & NOP instructions
 **/

void powerpc_cpu::execute_illegal(uint32 opcode)
{
	fprintf(stderr, "Illegal instruction at %08x, opcode = %08x\n", pc(), opcode);
	
	// Backtrace: walk PPC stack frames to show call chain
	fprintf(stderr, "  PPC Backtrace (stack frame walk):\n");
	{
		uint32 sp = gpr(1);
		uint32 ret_lr = lr();
		fprintf(stderr, "    frame 0: PC=0x%08x LR=0x%08x SP=0x%08x\n", pc(), ret_lr, sp);
		for (int frame = 1; frame < 12 && sp != 0 && sp < 0x50000000; frame++) {
			uint32 prev_sp = vm_read_memory_4(sp);  // backchain pointer
			if (prev_sp == 0 || prev_sp <= sp || prev_sp >= 0x50000000) break;
			uint32 saved_lr = vm_read_memory_4(prev_sp + 8);  // saved LR in caller's frame
			uint32 call_instr = 0;
			if (saved_lr >= 4 && saved_lr < 0x50000000)
				call_instr = vm_read_memory_4(saved_lr - 4);
			fprintf(stderr, "    frame %d: saved_LR=0x%08x SP=0x%08x call_instr=0x%08x\n",
					frame, saved_lr, prev_sp, call_instr);
			sp = prev_sp;
		}
	}

	// Dump PPC register state for crash analysis
	fprintf(stderr, "  LR=0x%08x CTR=0x%08x CR=0x%08x XER=0x%08x\n",
			lr(), ctr(), cr().get(), xer().get());
	fprintf(stderr, "  R0=0x%08x R1(SP)=0x%08x R2(TOC)=0x%08x R3=0x%08x\n",
			gpr(0), gpr(1), gpr(2), gpr(3));
	fprintf(stderr, "  R4=0x%08x R5=0x%08x R6=0x%08x R7=0x%08x\n",
			gpr(4), gpr(5), gpr(6), gpr(7));
	fprintf(stderr, "  R8=0x%08x R9=0x%08x R10=0x%08x R11=0x%08x\n",
			gpr(8), gpr(9), gpr(10), gpr(11));
	fprintf(stderr, "  R12=0x%08x R13=0x%08x\n", gpr(12), gpr(13));
	// Dump instructions around the crash address
	fprintf(stderr, "  Instructions around PC:\n");
	for (int di = -4; di <= 4; di++) {
		uint32 addr = pc() + di * 4;
		uint32 instr = vm_read_memory_4(addr);
		fprintf(stderr, "    [0x%08x] %08x%s\n", addr, instr, di == 0 ? " <-- CRASH" : "");
	}
	// Dump a few words at LR to help understand call chain
	fprintf(stderr, "  Instructions at LR 0x%08x:\n", lr());
	for (int di = -2; di <= 2; di++) {
		uint32 addr = lr() + di * 4;
		uint32 instr = vm_read_memory_4(addr);
		fprintf(stderr, "    [0x%08x] %08x\n", addr, instr);
	}

	// --- Forensic instrumentation: dump bl-target stub, TVECT-candidate
	// register memory, stack TOC slot, extended frame walk, AND extended
	// pre-call disasm + TVECT-load probe per frame. One-shot guard so repeated
	// illegal-instr hits don't flood the host log.
	//
	// Goal: identify which Sims data structure / CFM TVECT is bound to
	// {0x50010000, 0x50010000}.
#ifdef SHEEPSHAVER
	{
		static bool cycle9_dumped = false;
		if (!cycle9_dumped) {
			cycle9_dumped = true;

			// Local helper: is `addr` plausibly inside guest RAM or ROM?
			// We range-check before any ReadMacInt32 to avoid host SIGSEGV-in-SIGILL.
			auto in_guest_range = [](uint32 addr) -> bool {
				if (RAMBase != 0 && addr >= RAMBase && addr < (RAMBase + RAMSize)) return true;
				if (ROMBase != 0 && addr >= ROMBase && addr < (ROMBase + ROM_SIZE)) return true;
				return false;
			};

			fprintf(stderr, "  ---- forensic dump (one-shot) ----\n");

			// (1) bl-target stub disassembly.
			// LR points just past the bl. call_pc = LR - 4. Decode bl-immediate:
			// PPC bl: bits 0..5 = 18 (opcode), bits 6..29 = LI (24-bit signed
			// halfword offset), bit 30 = AA, bit 31 = LK.
			// target = (AA ? 0 : call_pc) + sign_extend(LI << 2)
			uint32 cur_lr = lr();
			uint32 call_pc = cur_lr - 4;
			fprintf(stderr, "  bl-target stub disassembly (call_pc=0x%08x):\n", call_pc);
			if (in_guest_range(call_pc)) {
				uint32 bl_instr = vm_read_memory_4(call_pc);
				uint32 opcode_pri = (bl_instr >> 26) & 0x3f;
				if (opcode_pri == 18) {
					// 26-bit sign-extended displacement (LI<<2 + AA + LK bits).
					int32 disp = (int32)(bl_instr & 0x03fffffc);
					if (disp & 0x02000000) disp |= 0xfc000000;  // sign-extend bit 25
					uint32 aa = (bl_instr >> 1) & 1;
					uint32 bl_target = (aa ? 0u : call_pc) + (uint32)disp;
					fprintf(stderr, "    bl_instr=0x%08x  disp=0x%08x  AA=%u  -> bl_target=0x%08x\n",
							bl_instr, (uint32)disp, aa, bl_target);
					if (in_guest_range(bl_target)) {
						for (int i = 0; i < 16; i++) {
							uint32 a = bl_target + i * 4;
							uint32 w = vm_read_memory_4(a);
							fprintf(stderr, "      [0x%08x] %08x\n", a, w);
						}
					} else {
						fprintf(stderr, "      bl_target OUT_OF_RANGE\n");
					}
				} else {
					fprintf(stderr, "    instr at LR-4 is not bl (primary op = %u, raw=0x%08x)\n",
							opcode_pri, bl_instr);
				}
			} else {
				fprintf(stderr, "    call_pc OUT_OF_RANGE\n");
			}

			// (2) TVECT-candidate register memory.
			// For each register that might hold a TVECT pointer, if its value is
			// inside guest range, read 8 bytes as {code, toc}. If both words
			// equal 0x50010000 we have a smoking gun.
			fprintf(stderr, "  TVECT-candidate register-memory dump:\n");
			static const int candidate_regs[] = {5, 6, 7, 8, 11, 12};
			for (size_t i = 0; i < sizeof(candidate_regs)/sizeof(candidate_regs[0]); i++) {
				int rn = candidate_regs[i];
				uint32 v = gpr(rn);
				if (in_guest_range(v)) {
					uint32 w0 = vm_read_memory_4(v);
					uint32 w1 = vm_read_memory_4(v + 4);
					const char *marker = (w0 == 0x50010000 && w1 == 0x50010000) ? "  <-- SMOKING GUN" : "";
					fprintf(stderr, "    r%-2d=0x%08x -> [code=0x%08x toc=0x%08x]%s\n",
							rn, v, w0, w1, marker);
				} else {
					fprintf(stderr, "    r%-2d=0x%08x  OUT_OF_RANGE\n", rn, v);
				}
			}

			// (3) Stack TOC slot (sp + 20).
			// CFM cross-fragment ABI saves the caller's TOC at sp+20 across calls.
			// `lwz r2, 20(r1)` at LR is the TOC restore; the slot itself tells us
			// what the resolved TVECT's TOC was for the call that returned here.
			uint32 sp = gpr(1);
			fprintf(stderr, "  Stack TOC slot (sp + 20):\n");
			if (in_guest_range(sp + 20)) {
				uint32 saved_toc = vm_read_memory_4(sp + 20);
				const char *marker = (saved_toc == 0x50010000) ? "  <-- SMOKING GUN (CFM TOC was 0x50010000)" : "";
				fprintf(stderr, "    [0x%08x] saved_toc=0x%08x%s\n", sp + 20, saved_toc, marker);
			} else {
				fprintf(stderr, "    sp+20=0x%08x OUT_OF_RANGE\n", sp + 20);
			}

			// (4) Stack frame walk (up to 4 frames): saved_LR + saved_TOC + saved_TOC_at_caller_sp.
			// The existing PPC Backtrace block (above) already prints saved_LR per frame.
			// Here we additionally extract each caller's saved TOC slot (caller_sp + 20)
			// and the saved LR's preceding instruction's bl-target if it's a bl.
			// We also collect (saved_LR, saved_TOC) pairs into arrays for the
			// extended pre-call disasm below.
			fprintf(stderr, "  Extended frame walk (saved_LR + saved_TOC + bl-target):\n");
			uint32 frame_saved_lr[4]  = {0, 0, 0, 0};
			uint32 frame_saved_toc[4] = {0, 0, 0, 0};
			int    n_frames_walked    = 0;
			{
				uint32 walk_sp = gpr(1);
				for (int frame = 0; frame < 4; frame++) {
					if (!in_guest_range(walk_sp)) {
						fprintf(stderr, "    frame %d: sp=0x%08x OUT_OF_RANGE\n", frame, walk_sp);
						break;
					}
					uint32 prev_sp = vm_read_memory_4(walk_sp);
					if (prev_sp == 0 || prev_sp <= walk_sp || !in_guest_range(prev_sp)) {
						fprintf(stderr, "    frame %d: backchain terminated (prev_sp=0x%08x)\n", frame, prev_sp);
						break;
					}
					uint32 saved_lr  = in_guest_range(prev_sp + 8)  ? vm_read_memory_4(prev_sp + 8)  : 0;
					uint32 saved_toc = in_guest_range(prev_sp + 20) ? vm_read_memory_4(prev_sp + 20) : 0;
					uint32 caller_bl_target = 0;
					if (saved_lr >= 4 && in_guest_range(saved_lr - 4)) {
						uint32 caller_bl = vm_read_memory_4(saved_lr - 4);
						uint32 caller_op = (caller_bl >> 26) & 0x3f;
						if (caller_op == 18) {
							int32 d = (int32)(caller_bl & 0x03fffffc);
							if (d & 0x02000000) d |= 0xfc000000;
							uint32 aa = (caller_bl >> 1) & 1;
							caller_bl_target = (aa ? 0u : (saved_lr - 4)) + (uint32)d;
						}
					}
					fprintf(stderr, "    frame %d: sp=0x%08x prev_sp=0x%08x saved_LR=0x%08x saved_TOC=0x%08x bl_target=0x%08x\n",
							frame, walk_sp, prev_sp, saved_lr, saved_toc, caller_bl_target);
					frame_saved_lr[frame]  = saved_lr;
					frame_saved_toc[frame] = saved_toc;
					n_frames_walked        = frame + 1;
					walk_sp = prev_sp;
				}
			}

			fprintf(stderr, "  ---- end forensic dump ----\n");

			// --------------------------------------------------------------
			// Extended pre-call disassembly + TVECT-load probe.
			//
			// For frame 0 (current LR) and frames 1..3 from the walk, dump 32
			// instructions backward from the call site (LR - 128 .. LR - 4) plus
			// the call instruction itself, with a minimal mnemonic guess. While
			// scanning, identify any `lwz r12, X(rN)` (primary op = 32, RT = 12)
			// or `addi r12, rN, X` (primary op = 14, RT = 12). For each match,
			// log it; if rN == 2 (TOC-relative), compute TVECT addr using the
			// frame's saved TOC and read 8 bytes there, marking <-- SMOKING GUN
			// if the contents are {0x50010000, 0x50010000}.
			//
			// Rationale: the trace confirmed the bl-target at 0x289fc0ec is the
			// canonical 5-instruction CFM cross-fragment glue (lwz r0,0(r12);
			// stw r2,20(r1); mtctr r0; lwz r2,4(r12); bctr). The TVECT pointer
			// arrives in r12 from a load issued in the CALLER (Sims code at LR-N).
			// We need to find that load and decode the TVECT address.
			fprintf(stderr, "  ---- pre-call disasm + TVECT probe (one-shot) ----\n");

			// Lambda: minimal PPC mnemonic guess for a 32-bit word. Returns a
			// short static-format string suitable for trailing-comment use. Only
			// the instructions we care about for TVECT setup are decoded; everything
			// else returns an empty string. Caller appends to fprintf format.
			auto guess_mnemonic = [](uint32 w, char *buf, size_t buflen) -> void {
				uint32 op  = (w >> 26) & 0x3f;
				uint32 rt  = (w >> 21) & 0x1f;
				uint32 ra  = (w >> 16) & 0x1f;
				int32  d16 = (int32)(int16)(w & 0xffff);
				if (op == 32) {                                    // lwz RT, D(RA)
					snprintf(buf, buflen, "lwz r%u, %d(r%u)", rt, d16, ra);
				} else if (op == 36) {                             // stw RT, D(RA)
					snprintf(buf, buflen, "stw r%u, %d(r%u)", rt, d16, ra);
				} else if (op == 14) {                             // addi RT, RA, SIMM
					if (ra == 0)
						snprintf(buf, buflen, "li r%u, %d", rt, d16);
					else
						snprintf(buf, buflen, "addi r%u, r%u, %d", rt, ra, d16);
				} else if (op == 15) {                             // addis RT, RA, SIMM
					if (ra == 0)
						snprintf(buf, buflen, "lis r%u, 0x%04x", rt, (uint16)(w & 0xffff));
					else
						snprintf(buf, buflen, "addis r%u, r%u, 0x%04x", rt, ra, (uint16)(w & 0xffff));
				} else if (op == 24) {                             // ori RT, RA, UIMM
					snprintf(buf, buflen, "ori r%u, r%u, 0x%04x", rt, ra, (uint16)(w & 0xffff));
				} else if (op == 18) {                             // b / bl / ba / bla
					int32 disp = (int32)(w & 0x03fffffc);
					if (disp & 0x02000000) disp |= 0xfc000000;
					uint32 lk = w & 1;
					uint32 aa = (w >> 1) & 1;
					snprintf(buf, buflen, "b%s%s disp=%d", lk ? "l" : "", aa ? "a" : "", disp);
				} else if (op == 19) {                             // XO=528 bctr/bctrl, 16 bclr
					uint32 xo = (w >> 1) & 0x3ff;
					if (xo == 528)
						snprintf(buf, buflen, "bctr%s", (w & 1) ? "l" : "");
					else if (xo == 16)
						snprintf(buf, buflen, "bclr%s", (w & 1) ? "l" : "");
					else
						buf[0] = '\0';
				} else if (op == 31) {                             // XFX (mtspr/mfspr/etc.)
					uint32 xo  = (w >> 1) & 0x3ff;
					uint32 spr = ((w >> 11) & 0x3e0) | ((w >> 16) & 0x1f);  // split SPR field
					if (xo == 467) {                                // mtspr
						if (spr == 9)        snprintf(buf, buflen, "mtctr r%u", rt);
						else if (spr == 8)   snprintf(buf, buflen, "mtlr r%u",  rt);
						else                 snprintf(buf, buflen, "mtspr %u, r%u", spr, rt);
					} else if (xo == 339) {                         // mfspr
						if (spr == 9)        snprintf(buf, buflen, "mfctr r%u", rt);
						else if (spr == 8)   snprintf(buf, buflen, "mflr r%u",  rt);
						else                 snprintf(buf, buflen, "mfspr r%u, %u", rt, spr);
					} else {
						buf[0] = '\0';
					}
				} else {
					buf[0] = '\0';
				}
			};

			// Lambda: dump 32 instructions backward from call_pc, plus the call
			// itself, and scan for r12-loading patterns. For each match log a
			// "Decoded TVECT load" line; if it's TOC-relative (RA=2) and a saved
			// TOC is available, dereference and check for the smoking gun.
			auto dump_pre_call = [&](int frame_idx, uint32 the_lr, uint32 saved_toc) {
				if (!in_guest_range(the_lr) || the_lr < 4) {
					fprintf(stderr, "  frame %d: LR=0x%08x OUT_OF_RANGE (skipping)\n", frame_idx, the_lr);
					return;
				}
				uint32 the_call_pc = the_lr - 4;
				fprintf(stderr, "  frame %d: pre-call disasm  LR=0x%08x  call_pc=0x%08x  saved_TOC=0x%08x\n",
						frame_idx, the_lr, the_call_pc, saved_toc);

				// Disasm window: 32 instructions back, plus the call itself.
				// addr range: [call_pc - 124, call_pc].
				const int kBack = 32;
				for (int idx = -kBack; idx <= 0; idx++) {
					uint32 addr = the_call_pc + idx * 4;
					if (!in_guest_range(addr)) {
						fprintf(stderr, "      [0x%08x] OUT_OF_RANGE\n", addr);
						continue;
					}
					uint32 w = vm_read_memory_4(addr);
					char mnem[64];
					guess_mnemonic(w, mnem, sizeof(mnem));
					const char *marker = (idx == 0) ? "  <-- bl call site" : "";
					if (mnem[0])
						fprintf(stderr, "      [0x%08x] %08x  # %s%s\n", addr, w, mnem, marker);
					else
						fprintf(stderr, "      [0x%08x] %08x%s\n", addr, w, marker);
				}

				// Scan again for r12-establishing instructions:
				//   - lwz  r12, D(RA)        primary op = 32, RT = 12
				//   - addi r12, RA, D        primary op = 14, RT = 12
				//   - lis  r12, UIMM         primary op = 15, RT = 12, RA = 0
				//   - ori  r12, r12, UIMM    primary op = 24, RT = 12, RA = 12
				// For each lwz match with RA == 2 (TOC-relative) and a saved TOC,
				// dereference the TVECT and report.
				bool any_match = false;
				for (int idx = -kBack; idx <= 0; idx++) {
					uint32 addr = the_call_pc + idx * 4;
					if (!in_guest_range(addr)) continue;
					uint32 w = vm_read_memory_4(addr);
					uint32 op = (w >> 26) & 0x3f;
					uint32 rt = (w >> 21) & 0x1f;
					uint32 ra = (w >> 16) & 0x1f;
					int32  d  = (int32)(int16)(w & 0xffff);
					if (op == 32 && rt == 12) {
						// lwz r12, D(RA)
						any_match = true;
						fprintf(stderr, "    Decoded r12 load: instr=[0x%08x] %08x lwz r12, %d(r%u)\n",
								addr, w, d, ra);
						if (ra == 2 && saved_toc != 0 && in_guest_range(saved_toc + (uint32)d)) {
							uint32 tvect_addr = saved_toc + (uint32)d;
							uint32 code = vm_read_memory_4(tvect_addr);
							uint32 toc  = in_guest_range(tvect_addr + 4) ? vm_read_memory_4(tvect_addr + 4) : 0;
							const char *marker = (code == 0x50010000 && toc == 0x50010000) ? "  <-- SMOKING GUN" : "";
							fprintf(stderr, "      TOC-relative TVECT probe: tvect_addr=0x%08x -> [code=0x%08x toc=0x%08x]%s\n",
									tvect_addr, code, toc, marker);
						} else if (ra == 2 && saved_toc != 0) {
							fprintf(stderr, "      TOC-relative TVECT probe: tvect_addr=0x%08x OUT_OF_RANGE\n",
									(uint32)(saved_toc + (uint32)d));
						} else if (ra != 2) {
							fprintf(stderr, "      (non-TOC base r%u; cannot resolve without runtime register value)\n", ra);
						}
					} else if (op == 14 && rt == 12) {
						// addi r12, RA, D
						any_match = true;
						if (ra == 0)
							fprintf(stderr, "    Decoded r12 load: instr=[0x%08x] %08x li r12, %d\n",
									addr, w, d);
						else
							fprintf(stderr, "    Decoded r12 load: instr=[0x%08x] %08x addi r12, r%u, %d\n",
									addr, w, ra, d);
					} else if (op == 15 && rt == 12 && ra == 0) {
						// lis r12, UIMM
						any_match = true;
						fprintf(stderr, "    Decoded r12 load: instr=[0x%08x] %08x lis r12, 0x%04x\n",
								addr, w, (uint16)(w & 0xffff));
					} else if (op == 24 && rt == 12 && ra == 12) {
						// ori r12, r12, UIMM (typical lis/ori 32-bit immediate pair)
						any_match = true;
						fprintf(stderr, "    Decoded r12 load: instr=[0x%08x] %08x ori r12, r12, 0x%04x\n",
								addr, w, (uint16)(w & 0xffff));
					}
				}
				if (!any_match) {
					fprintf(stderr, "    (no r12-establishing instruction found in window; consider extending to LR-256)\n");
				}
			};

			// Frame 0: current LR + saved TOC from sp+20 (already read above).
			{
				uint32 sp0 = gpr(1);
				uint32 frame0_saved_toc = in_guest_range(sp0 + 20) ? vm_read_memory_4(sp0 + 20) : 0;
				dump_pre_call(0, lr(), frame0_saved_toc);
			}
			// Frames 1..3: saved_LR + saved_TOC collected during the extended walk.
			for (int f = 1; f < n_frames_walked; f++) {
				dump_pre_call(f, frame_saved_lr[f - 1], frame_saved_toc[f - 1]);
			}

			fprintf(stderr, "  ---- end pre-call disasm dump ----\n");

			// --------------------------------------------------------------
			// TOC-window TVECT scan.
			//
			// The trace confirmed that pinning the CFM glue-stub PC and dumping
			// r12 at entry is unreliable because Sims's CFM container relocates
			// each launch (one run's bl_target 0x289efc2c; another run's bl_target
			// 0x28ac78ec — same 5-instr glue, different absolute address).
			// Instead of pinning a PC, scan the import region of Sims's CFM
			// fragment for any 8-byte aligned pair == {0x50010000, 0x50010000}.
			//
			// Scope of scan:
			//   (a) 64 KiB ABOVE and 64 KiB BELOW frame-0 saved_toc, 8-byte
			//       stride. Covers Sims's standard imports section near TOC.
			//   (b) The entire SheepMem region [0x60000000, 0x60080000) —
			//       guest scratch reservations used by PocketShaver for native
			//       TVECTs (RAVE/DSp/OpenGL). If Sims happens to import a
			//       symbol whose loader-resolution writes here, the bogus
			//       TVECT will be in this range, not near the Sims TOC.
			//
			// Output: first 64 matches with address, offset-from-TOC (or
			//   relative to SheepMem base), and a 16-instruction surrounding
			//   window so neighboring TVECTs are visible.
			fprintf(stderr, "  ---- TOC-window TVECT scan (one-shot) ----\n");
			{
				// SheepMem base/size are protected; the Unix layer (used by
				// PocketShaver / Mac Catalyst) hardcodes base = 0x60000000 and
				// size = 0x80000 (see SheepShaver/src/Unix/main_unix.cpp:277
				// and SheepShaver/src/include/thunks.h:132). We mirror those
				// constants here rather than adding a public accessor.
				const uint32 kSheepMemBase = 0x60000000;
				const uint32 kSheepMemSize = 0x00080000;
				const uint32 kSheepMemEnd  = kSheepMemBase + kSheepMemSize;
				const uint32 kBogus = 0x50010000;
				const int    kMaxMatches = 64;
				int matches_found = 0;
				uint32 last_match_addr = 0;
				(void)last_match_addr;
				
				// Frame-0 saved_toc (read fresh from sp+20). Trace evidence
				// example: 0x28b57e60. We make no assumption about its value;
				// if OUT_OF_RANGE the TOC-window pass is skipped and we only
				// do the SheepMem pass.
				uint32 sp_for_toc = gpr(1);
				uint32 frame0_toc = in_guest_range(sp_for_toc + 20)
				                  ? vm_read_memory_4(sp_for_toc + 20)
				                  : 0;
				fprintf(stderr, "    frame-0 saved_toc=0x%08x (used as scan center)\n", frame0_toc);
				
				// Inner helper: surround-dump 16 instructions (8 above, 8 at-or-after)
				// at the match address, range-checked. Called only on confirmed match.
				auto dump_surrounding = [&](uint32 match_addr, const char *region_tag, int64_t offset_from_anchor) {
					fprintf(stderr, "    [match %d] addr=0x%08x  %s  offset=%+lld  contents=[0x50010000, 0x50010000]\n",
							matches_found, match_addr, region_tag, (long long)offset_from_anchor);
					for (int di = -8; di < 8; di++) {
						uint32 a = match_addr + di * 4;
						if (!in_guest_range(a) &&
						    !(a >= kSheepMemBase && a < kSheepMemEnd)) {
							fprintf(stderr, "        [0x%08x] OUT_OF_RANGE\n", a);
							continue;
						}
						uint32 w = vm_read_memory_4(a);
						const char *marker = (di == 0) ? "  <-- match" : "";
						fprintf(stderr, "        [0x%08x] %08x%s\n", a, w, marker);
					}
				};
				
				// Pass (a): TOC-centered window. 64 KiB above + 64 KiB below,
				// 8-byte stride (TVECT alignment). Range-check each candidate
				// addr against guest range AND against SheepMem (since TOC could
				// happen to be near SheepMem boundary in pathological loaders).
				if (frame0_toc != 0) {
					const uint32 kWindow = 0x10000;  // 64 KiB each side
					uint32 lo = (frame0_toc > kWindow) ? (frame0_toc - kWindow) : 0;
					uint32 hi = frame0_toc + kWindow;
					// Align to 8 bytes.
					lo &= ~7u;
					hi &= ~7u;
					fprintf(stderr, "    pass (a): TOC-window scan [0x%08x .. 0x%08x), stride=8\n", lo, hi);
					int scanned = 0;
					int skipped_oor = 0;
					for (uint32 a = lo; a < hi && matches_found < kMaxMatches; a += 8) {
						if (!in_guest_range(a) || !in_guest_range(a + 4)) {
							skipped_oor++;
							continue;
						}
						scanned++;
						uint32 w0 = vm_read_memory_4(a);
						if (w0 != kBogus) continue;
						uint32 w1 = vm_read_memory_4(a + 4);
						if (w1 != kBogus) continue;
						int64_t off = (int64_t)a - (int64_t)frame0_toc;
						dump_surrounding(a, "TOC-window", off);
						matches_found++;
						last_match_addr = a;
					}
					fprintf(stderr, "    pass (a) done: scanned=%d, skipped_oor=%d, matches_so_far=%d\n",
							scanned, skipped_oor, matches_found);
				} else {
					fprintf(stderr, "    pass (a): SKIPPED (frame-0 saved_toc is 0)\n");
				}
				
				// Pass (b): SheepMem region scan, 8-byte stride.
				// Many entries in SheepMem are TVECTs allocated by PocketShaver
				// for RAVE / DSp / OpenGL public symbol bindings. A bogus
				// {0x50010000, 0x50010000} pair here would mean WE wrote it.
				// vm_read_memory_4 / Mac2HostAddr handles SheepMem the same way
				// as RAM since SheepMem is mapped contiguously (verified via
				// Mac2HostAddr -> RAMBaseHost + offset path in cpu_emulation.h).
				//
				// Note: in_guest_range() (the helper above) only accepts
				// RAM and ROM. SheepMem may or may not fall inside [RAMBase,
				// RAMBase+RAMSize). We probe defensively by computing Mac2HostAddr
				// inline — but `vm_read_memory_4` itself does that. So we just
				// call it; if the address is unmapped we'll SIGSEGV, which would
				// indicate SheepMem mapping moved.
				//
				// Safer alternative: gate pass (b) behind in_guest_range; if
				// SheepMem isn't in the RAM range, we skip the pass and log so.
				if (in_guest_range(kSheepMemBase) && in_guest_range(kSheepMemEnd - 8)) {
					fprintf(stderr, "    pass (b): SheepMem scan [0x%08x .. 0x%08x), stride=8\n",
							kSheepMemBase, kSheepMemEnd);
					int scanned = 0;
					for (uint32 a = kSheepMemBase; a < kSheepMemEnd && matches_found < kMaxMatches; a += 8) {
						scanned++;
						uint32 w0 = vm_read_memory_4(a);
						if (w0 != kBogus) continue;
						uint32 w1 = vm_read_memory_4(a + 4);
						if (w1 != kBogus) continue;
						int64_t off = (int64_t)a - (int64_t)kSheepMemBase;
						dump_surrounding(a, "SheepMem", off);
						matches_found++;
						last_match_addr = a;
					}
					fprintf(stderr, "    pass (b) done: scanned=%d, matches_so_far=%d\n",
							scanned, matches_found);
				} else {
					fprintf(stderr, "    pass (b): SheepMem region [0x%08x..0x%08x) not in guest_range — SKIPPED\n",
							kSheepMemBase, kSheepMemEnd);
				}
				
				fprintf(stderr, "    TOTAL matches found: %d (cap = %d)\n",
						matches_found, kMaxMatches);
				if (matches_found == 0) {
					fprintf(stderr, "    NO matches — widen the window (try 256 KiB above/below TOC, or scan full guest RAM in coarse chunks)\n");
				} else if (matches_found >= kMaxMatches) {
					fprintf(stderr, "    HIT CAP — many bogus TVECTs in window; likely bulk-init failure. Next: identify the loader write path.\n");
				}
			}
			fprintf(stderr, "  ---- end TOC-window TVECT scan dump ----\n");

			// --------------------------------------------------------------
			// Wider scan after the previous pass hit ZERO matches in the
			// 128 KiB TOC window. The bad TVECTs are NOT immediately adjacent
			// to Sims's TOC — they live somewhere else in Sims's loaded
			// fragment (heap-allocated CFM imports? PEF loader scratch?
			// SheepMem-side import resolution?). Three new passes:
			//
			//   (c) Sims-fragment wide scan: [0x28000000, 0x29000000) — 16 MB
			//       byte-stride scan via Mac2HostAddr where available, with
			//       vm_read_memory_4 fallback per 8-byte pair if direct host
			//       mapping fails. Fast path covers Sims's entire loaded
			//       fragment in one pass.
			//
			//   (d) Full guest RAM: [RAMBase, RAMBase+RAMSize) — only fires if
			//       pass (c) finds zero matches. Exhaustive but slower (still
			//       native memcmp via Mac2HostAddr on the RAMBaseHost block).
			//
			//   (e) SheepMem scan: [0x60000000, 0x60080000). The earlier pass
			//       skipped this because its in_guest_range lambda only
			//       admitted RAM/ROM. This pass uses a wider local helper
			//       in_scannable_range that also accepts SheepMem, so this
			//       pass actually runs.
			//
			// Output per match:
			//   - region tag (c/d/e)
			//   - absolute address
			//   - offset from frame-0 saved_toc (signed)
			//   - 16 surrounding instruction words (-8 .. +7, range-checked)
			//
			// Cap N=64 across passes. If any pass hits cap, subsequent passes
			// are short-circuited so the log stays bounded.
			fprintf(stderr, "  ---- wide TVECT scan (one-shot) ----\n");
			{
				const uint32 kSheepMemBase13 = 0x60000000;
				const uint32 kSheepMemSize13 = 0x00080000;
				const uint32 kSheepMemEnd13  = kSheepMemBase13 + kSheepMemSize13;
				const uint32 kBogus13 = 0x50010000;
				const int    kMaxMatches13 = 64;
				int matches13 = 0;

				// Wider local helper: accepts RAM, ROM, AND SheepMem.
				// Note: this is a copy of in_guest_range with SheepMem
				// added. We deliberately don't modify the original helper
				// (earlier pass results would change retroactively).
				auto in_scannable_range = [&](uint32 addr) -> bool {
					if (RAMBase != 0 && addr >= RAMBase && addr < (RAMBase + RAMSize)) return true;
					if (ROMBase != 0 && addr >= ROMBase && addr < (ROMBase + ROM_SIZE)) return true;
					if (addr >= kSheepMemBase13 && addr < kSheepMemEnd13) return true;
					return false;
				};

				// Pull frame-0 saved_toc again (it was already read above for
				// pass (a) but is scoped inside the earlier block). Reread
				// for this pass's offset reporting.
				uint32 sp_now = gpr(1);
				uint32 frame0_toc13 = in_scannable_range(sp_now + 20)
				                    ? vm_read_memory_4(sp_now + 20)
				                    : 0;
				fprintf(stderr, "    wide-scan anchor: frame-0 saved_toc=0x%08x\n", frame0_toc13);

				// Per-match dump helper (mirrors the earlier dump_surrounding
				// but uses in_scannable_range and tags region with the wide-scan
				// pass id).
				auto dump_match13 = [&](uint32 match_addr, const char *pass_tag) {
					int64_t off = (int64_t)match_addr - (int64_t)frame0_toc13;
					fprintf(stderr, "    [c13 match %d] addr=0x%08x  %s  off_from_toc=%+lld  contents=[0x50010000, 0x50010000]\n",
							matches13, match_addr, pass_tag, (long long)off);
					for (int di = -8; di < 8; di++) {
						uint32 a = match_addr + di * 4;
						if (!in_scannable_range(a)) {
							fprintf(stderr, "        [0x%08x] OUT_OF_RANGE\n", a);
							continue;
						}
						uint32 w = vm_read_memory_4(a);
						const char *marker = (di == 0) ? "  <-- match" : "";
						fprintf(stderr, "        [0x%08x] %08x%s\n", a, w, marker);
					}
				};

				// ---- Pass (c) Sims-fragment wide scan [0x28000000, 0x29000000)
				//
				// Frame-0 evidence across cycles places Sims's loaded fragment
				// in this 16 MB window (PCs observed: 0x28917898, 0x28857ad8,
				// 0x288622d8, ...; saved TOCs: 0x28a8c660, 0x28b41c20, ...;
				// bl_targets: 0x28ab16ac, 0x28ac78ec, 0x289fc0ec).
				//
				// Fast path: get a host pointer to 0x28000000 via Mac2HostAddr;
				// scan the host block with 8-byte stride memcmp against the
				// bogus pattern. If Mac2HostAddr returns null OR if any 8-byte
				// pair straddles a guest-range boundary, fall back to per-pair
				// vm_read_memory_4 for that pair.
				{
					const uint32 kSimsLo = 0x28000000;
					const uint32 kSimsHi = 0x29000000;  // exclusive
					fprintf(stderr, "    pass (c): Sims-fragment scan [0x%08x .. 0x%08x), stride=8\n",
							kSimsLo, kSimsHi);
					int scanned_c = 0;
					int skipped_c = 0;
					int matches_c_start = matches13;

					// Verify endpoints are in scannable range. If not, we still
					// proceed but per-pair check guards the read.
					bool lo_ok = in_scannable_range(kSimsLo);
					bool hi_ok = in_scannable_range(kSimsHi - 8);
					fprintf(stderr, "        scan endpoints: lo_in_range=%d hi_in_range=%d\n", lo_ok, hi_ok);

					// Determine whether we can do the fast native-host scan.
					// Mac2HostAddr is a thin wrapper around vm_do_get_real_address;
					// for guest RAM it returns RAMBaseHost + (addr - RAMBase).
					// We only fast-path if both endpoints are in guest range
					// AND host base pointer is non-null.
					uint8 *host_base = nullptr;
					bool use_fast = false;
					if (lo_ok && hi_ok) {
						host_base = Mac2HostAddr(kSimsLo);
						use_fast = (host_base != nullptr);
					}
					fprintf(stderr, "        fast-path host_base=%p use_fast=%d\n",
							(void *)host_base, use_fast ? 1 : 0);

					if (use_fast) {
						// Big-endian on guest: each uint32 0x50010000 is bytes
						// {0x50, 0x01, 0x00, 0x00}. An 8-byte TVECT pair is
						// {0x50, 0x01, 0x00, 0x00, 0x50, 0x01, 0x00, 0x00}.
						static const uint8 pat[8] = {0x50, 0x01, 0x00, 0x00, 0x50, 0x01, 0x00, 0x00};
						size_t span = (size_t)(kSimsHi - kSimsLo);
						for (size_t off = 0; off + 8 <= span && matches13 < kMaxMatches13; off += 8) {
							scanned_c++;
							if (memcmp(host_base + off, pat, 8) != 0) continue;
							uint32 a = kSimsLo + (uint32)off;
							dump_match13(a, "Sims-fragment(fast)");
							matches13++;
						}
					} else {
						// Slow fallback: per-pair vm_read_memory_4. ~2M reads
						// for 16 MB / 8 bytes; acceptable since this fires
						// once per SIGILL and aborts after.
						for (uint32 a = kSimsLo; a + 8 <= kSimsHi && matches13 < kMaxMatches13; a += 8) {
							if (!in_scannable_range(a) || !in_scannable_range(a + 4)) {
								skipped_c++;
								continue;
							}
							scanned_c++;
							uint32 w0 = vm_read_memory_4(a);
							if (w0 != kBogus13) continue;
							uint32 w1 = vm_read_memory_4(a + 4);
							if (w1 != kBogus13) continue;
							dump_match13(a, "Sims-fragment(slow)");
							matches13++;
						}
					}
					fprintf(stderr, "    pass (c) done: scanned=%d, skipped_oor=%d, matches_this_pass=%d, matches_total=%d\n",
							scanned_c, skipped_c, matches13 - matches_c_start, matches13);
				}

				// ---- Pass (d) Full guest RAM scan [RAMBase, RAMBase+RAMSize)
				//
				// Only run if pass (c) found zero. RAMSize on PocketShaver is
				// typically 64-256 MB; even at 256 MB the native memcmp pass
				// finishes in ~250 ms — acceptable for a one-shot crash dump.
				if (matches13 == 0 && RAMBase != 0 && RAMSize > 0) {
					fprintf(stderr, "    pass (d): full RAM scan [0x%08x .. 0x%08x), stride=8\n",
							RAMBase, RAMBase + RAMSize);
					int matches_d_start = matches13;
					int scanned_d = 0;
					uint8 *ram_host = Mac2HostAddr(RAMBase);
					if (ram_host != nullptr) {
						static const uint8 pat[8] = {0x50, 0x01, 0x00, 0x00, 0x50, 0x01, 0x00, 0x00};
						size_t span = (size_t)RAMSize;
						for (size_t off = 0; off + 8 <= span && matches13 < kMaxMatches13; off += 8) {
							scanned_d++;
							if (memcmp(ram_host + off, pat, 8) != 0) continue;
							uint32 a = RAMBase + (uint32)off;
							dump_match13(a, "full-RAM(fast)");
							matches13++;
						}
						fprintf(stderr, "    pass (d) done: scanned=%d, matches_this_pass=%d, matches_total=%d\n",
								scanned_d, matches13 - matches_d_start, matches13);
					} else {
						fprintf(stderr, "    pass (d): Mac2HostAddr(RAMBase) returned null — SKIPPED\n");
					}
				} else if (matches13 > 0) {
					fprintf(stderr, "    pass (d): SKIPPED (pass (c) already found %d matches)\n", matches13);
				} else {
					fprintf(stderr, "    pass (d): SKIPPED (RAMBase=0x%08x RAMSize=0x%08x)\n", RAMBase, RAMSize);
				}

				// ---- Pass (e) SheepMem scan [0x60000000, 0x60080000)
				//
				// Cycle 12 skipped this because its in_guest_range only
				// admitted RAM/ROM. Cycle 13 uses in_scannable_range (which
				// includes SheepMem) AND tries Mac2HostAddr for native scan.
				if (matches13 < kMaxMatches13) {
					fprintf(stderr, "    pass (e): SheepMem scan [0x%08x .. 0x%08x), stride=8\n",
							kSheepMemBase13, kSheepMemEnd13);
					int matches_e_start = matches13;
					int scanned_e = 0;
					int skipped_e = 0;
					uint8 *sm_host = Mac2HostAddr(kSheepMemBase13);
					if (sm_host != nullptr) {
						static const uint8 pat[8] = {0x50, 0x01, 0x00, 0x00, 0x50, 0x01, 0x00, 0x00};
						size_t span = (size_t)kSheepMemSize13;
						for (size_t off = 0; off + 8 <= span && matches13 < kMaxMatches13; off += 8) {
							scanned_e++;
							if (memcmp(sm_host + off, pat, 8) != 0) continue;
							uint32 a = kSheepMemBase13 + (uint32)off;
							dump_match13(a, "SheepMem(fast)");
							matches13++;
						}
						fprintf(stderr, "    pass (e) done: scanned=%d, skipped_oor=%d, matches_this_pass=%d, matches_total=%d\n",
								scanned_e, skipped_e, matches13 - matches_e_start, matches13);
					} else {
						// Try slow path with vm_read_memory_4 — but only if
						// at least the base address resolves as scannable.
						if (in_scannable_range(kSheepMemBase13)) {
							for (uint32 a = kSheepMemBase13; a + 8 <= kSheepMemEnd13 && matches13 < kMaxMatches13; a += 8) {
								scanned_e++;
								uint32 w0 = vm_read_memory_4(a);
								if (w0 != kBogus13) continue;
								uint32 w1 = vm_read_memory_4(a + 4);
								if (w1 != kBogus13) continue;
								dump_match13(a, "SheepMem(slow)");
								matches13++;
							}
							fprintf(stderr, "    pass (e) slow: scanned=%d, matches_this_pass=%d, matches_total=%d\n",
									scanned_e, matches13 - matches_e_start, matches13);
						} else {
							fprintf(stderr, "    pass (e): SheepMem not scannable (Mac2HostAddr null AND not in_scannable_range) — SKIPPED\n");
						}
					}
				} else {
					fprintf(stderr, "    pass (e): SKIPPED (already at cap)\n");
				}

				fprintf(stderr, "    CYCLE-13 TOTAL matches found: %d (cap = %d)\n",
						matches13, kMaxMatches13);
				if (matches13 == 0) {
					fprintf(stderr, "    NO matches across passes (c/d/e). Bogus TVECTs are written THEN OVERWRITTEN before SIGILL — switch to write-site watchpoint in cycle 14 (instrument any 4-byte write of 0x50010000 to guest RAM).\n");
				} else if (matches13 >= kMaxMatches13) {
					fprintf(stderr, "    HIT CAP — many bogus TVECTs. Next: identify the loader write path. Candidate sources: macos_util.cpp (FindLibSymbol), thunks.cpp (CFM import init), rom_patches.cpp.\n");
				} else {
					fprintf(stderr, "    Got %d match(es). Next: grep-trace who writes 0x50010000 to those addresses. Candidate files: macos_util.cpp, thunks.cpp, rom_patches.cpp; or a SUM (RAMBase+X / ROMBase+X) producing 0x50010000.\n", matches13);
				}
			}
			fprintf(stderr, "  ---- end wide TVECT scan dump ----\n");

			// --------------------------------------------------------------
			// Pass (f): ROM scan.
			//
			// The wide scan covered Sims-fragment (16 MB), full guest RAM (512 MB),
			// and SheepMem (512 KiB) — all zero matches. The remaining
			// region NOT yet scanned is ROM itself: [ROMBase, ROMBase+ROM_SIZE).
			// Note that 0x50010000 = ROMBase + 0x10000, i.e. inside the early-
			// ROM PEF/data region. It is plausible that a baked-in TVECT
			// somewhere in ROM contains {0x50010000, 0x50010000} as an "unbound
			// import sentinel" or similar default. ROM is 4 MB → cheap scan.
			//
			// If a hit lands here, Sims's vtable / CFM imports point INTO ROM
			// and the bad TVECT is ROM-baked. The fix shape then becomes
			// ROM-patch / rsrc-patch / glue-stub-redirect (not loader write fix).
			fprintf(stderr, "  ---- ROM scan (one-shot) ----\n");
			{
				const uint32 kBogus14 = 0x50010000;
				const int kMaxMatches14 = 64;
				int matches14 = 0;
				if (ROMBase == 0 || ROM_SIZE == 0) {
					fprintf(stderr, "    pass (f): ROM not mapped (ROMBase=0x%08x ROM_SIZE=0x%08x) — SKIPPED\n",
							ROMBase, ROM_SIZE);
				} else {
					uint32 sp_now_f = gpr(1);
					uint32 frame0_toc14 = 0;
					if (RAMBase != 0
					 && (sp_now_f + 20) >= RAMBase
					 && (sp_now_f + 20) < (RAMBase + RAMSize)) {
						frame0_toc14 = vm_read_memory_4(sp_now_f + 20);
					}
					fprintf(stderr, "    pass (f): ROM scan [0x%08x .. 0x%08x), stride=8\n",
							ROMBase, ROMBase + ROM_SIZE);
					fprintf(stderr, "        ROM-scan anchor: frame-0 saved_toc=0x%08x\n", frame0_toc14);

					uint8 *rom_host = Mac2HostAddr(ROMBase);
					int scanned_f = 0;
					if (rom_host != nullptr) {
						static const uint8 pat_f[8] = {0x50, 0x01, 0x00, 0x00, 0x50, 0x01, 0x00, 0x00};
						size_t span = (size_t)ROM_SIZE;
						fprintf(stderr, "        fast-path host_base=%p\n", rom_host);
						for (size_t off = 0; off + 8 <= span && matches14 < kMaxMatches14; off += 8) {
							scanned_f++;
							if (memcmp(rom_host + off, pat_f, 8) != 0) continue;
							uint32 a = ROMBase + (uint32)off;
							int64_t off_from_toc = (int64_t)a - (int64_t)frame0_toc14;
							fprintf(stderr, "    [c14 match %d] addr=0x%08x  ROM(fast)  rom_off=+0x%08x  off_from_toc=%+lld  contents=[0x50010000, 0x50010000]\n",
									matches14, a, (uint32)off, (long long)off_from_toc);
							// 16-word surrounding dump (range-checked).
							for (int di = -8; di < 8; di++) {
								uint32 wa = a + di * 4;
								bool in_rom = (wa >= ROMBase && wa < (ROMBase + ROM_SIZE));
								if (!in_rom) {
									fprintf(stderr, "        [0x%08x] OUT_OF_ROM\n", wa);
									continue;
								}
								uint32 w = vm_read_memory_4(wa);
								const char *marker = (di == 0) ? "  <-- match" : "";
								fprintf(stderr, "        [0x%08x] %08x%s\n", wa, w, marker);
							}
							matches14++;
						}
					} else {
						// Slow fallback: per-pair vm_read_memory_4. ROM is 4 MB
						// so this is still fine.
						fprintf(stderr, "        fast-path host_base=null — slow fallback\n");
						for (uint32 a = ROMBase; (a + 8) <= (ROMBase + ROM_SIZE) && matches14 < kMaxMatches14; a += 8) {
							scanned_f++;
							uint32 w0 = vm_read_memory_4(a);
							if (w0 != kBogus14) continue;
							uint32 w1 = vm_read_memory_4(a + 4);
							if (w1 != kBogus14) continue;
							int64_t off_from_toc = (int64_t)a - (int64_t)frame0_toc14;
							fprintf(stderr, "    [c14 match %d] addr=0x%08x  ROM(slow)  rom_off=+0x%08x  off_from_toc=%+lld  contents=[0x50010000, 0x50010000]\n",
									matches14, a, a - ROMBase, (long long)off_from_toc);
							matches14++;
						}
					}
					fprintf(stderr, "    pass (f) done: scanned=%d, matches_this_pass=%d\n",
							scanned_f, matches14);
				}
				fprintf(stderr, "    CYCLE-14 ROM-scan matches found: %d (cap = %d)\n",
						matches14, kMaxMatches14);
				if (matches14 == 0) {
					fprintf(stderr, "    NO ROM matches. TVECT not ROM-baked AND not transiently present. Watchpoint output (above, pre-SIGILL) should reveal write site.\n");
				} else {
					fprintf(stderr, "    Got %d ROM match(es). TVECT(s) are ROM-baked. Next step: identify Sims vtable/CFM-import slot that references this ROM offset, then choose ROM-patch / rsrc-patch / glue-stub-redirect.\n", matches14);
				}
			}
			fprintf(stderr, "  ---- end ROM scan ----\n");

			// --------------------------------------------------------------
			// Watchpoint summary: emit whatever the write-site
			// watchpoint accumulated up to this point. The watchpoint lives
			// inside execute_loadstore (and a couple of related store paths)
			// and accumulates unique (PC, target_addr) tuples that wrote
			// the value 0x50010000 to a guest-range address. See
			// cycle14_watchpoint_* statics at file scope.
			fprintf(stderr, "  ---- watchpoint summary ----\n");
			extern int cycle14_wp_count;
			extern int cycle14_wp_overflow;
			extern uint32 cycle14_wp_pcs[];
			extern uint32 cycle14_wp_lrs[];
			extern uint32 cycle14_wp_eas[];
			extern uint32 cycle14_wp_seen_at[];
			fprintf(stderr, "    unique (PC, addr) tuples logged: %d (cap=32)  overflow_dropped=%d\n",
					cycle14_wp_count, cycle14_wp_overflow);
			for (int i = 0; i < cycle14_wp_count; i++) {
				fprintf(stderr, "    [wp %d] PC=0x%08x LR=0x%08x target=0x%08x first_seen_at_block=%u\n",
						i, cycle14_wp_pcs[i], cycle14_wp_lrs[i], cycle14_wp_eas[i], cycle14_wp_seen_at[i]);
				// 8-instr window around the writing PC (-4 .. +3).
				for (int di = -4; di < 4; di++) {
					uint32 wa = cycle14_wp_pcs[i] + di * 4;
					bool in_ram = (RAMBase != 0 && wa >= RAMBase && wa < (RAMBase + RAMSize));
					bool in_rom = (ROMBase != 0 && wa >= ROMBase && wa < (ROMBase + ROM_SIZE));
					if (!in_ram && !in_rom) {
						fprintf(stderr, "        [0x%08x] OUT_OF_RANGE\n", wa);
						continue;
					}
					uint32 w = vm_read_memory_4(wa);
					const char *marker = (di == 0) ? "  <-- write PC" : "";
					fprintf(stderr, "        [0x%08x] %08x%s\n", wa, w, marker);
				}
			}
			if (cycle14_wp_count == 0) {
				fprintf(stderr, "    NO writes of 0x50010000 observed before SIGILL. Either the value is constructed via lis/ori then stored via stwu in a not-yet-instrumented path, or it never goes through guest memory at all (computed-then-bctr'd via register only). Next step: instrument lis/ori construction OR the bctr-target read.\n");
			} else {
				fprintf(stderr, "    Watchpoint captured %d distinct write site(s). Next step: open each writer PC in the codebase (or read the surrounding PPC instructions above) to identify what loader / lazy-binding handler wrote 0x50010000.\n", cycle14_wp_count);
			}
			fprintf(stderr, "  ---- end watchpoint summary ----\n");
		}
	}
#endif

#ifdef SHEEPSHAVER
	if (PrefsFindBool("ignoreillegal")) {
		increment_pc(4);
		return;
	}
#endif

#if ENABLE_MON
	disass_ppc(stdout, pc(), opcode);

	// Start up mon in real-mode
	const char *arg[4] = {"mon", "-m", "-r", NULL};
	mon(3, arg);
#endif
	abort();
}

void powerpc_cpu::execute_nop(uint32 opcode)
{
	increment_pc(4);
}

/**
 *  Floating-point rounding modes conversion
 **/

static inline int ppc_to_native_rounding_mode(int round)
{
	switch (round) {
	case 0: return FE_TONEAREST;
	case 1: return FE_TOWARDZERO;
	case 2: return FE_UPWARD;
	case 3: return FE_DOWNWARD;
	}
	return FE_TONEAREST;
}

/**
 *	Helper class to compute the overflow/carry condition
 *
 *		OP		Operation to perform
 */

template< class OP >
struct op_carry {
	static inline bool apply(uint32, uint32, uint32) {
		return false;
	}
};

template<>
struct op_carry<op_add> {
	static inline bool apply(uint32 a, uint32 b, uint32 c) {
		// TODO: use 32-bit arithmetics
		uint64 carry = (uint64)a + (uint64)b + (uint64)c;
		return (carry >> 32) != 0;
	}
};

template< class OP >
struct op_overflow {
	static inline bool apply(uint32, uint32, uint32) {
		return false;
	}
};

template<>
struct op_overflow<op_neg> {
	static inline bool apply(uint32 a, uint32, uint32) {
		return a == 0x80000000;
	};
};

template<>
struct op_overflow<op_add> {
	static inline bool apply(uint32 a, uint32 b, uint32 c) {
		// TODO: use 32-bit arithmetics
		int64 overflow = (int64)(int32)a + (int64)(int32)b + (int64)(int32)c;
		return (((uint64)overflow) >> 63) ^ (((uint32)overflow) >> 31);
	}
};

/**
 *	Perform an addition/substraction
 *
 *		RA		Input operand register, possibly 0
 *		RB		Input operand either register or immediate
 *		RC		Input carry
 *		CA		Predicate to compute the carry out of the operation
 *		OE		Predicate to compute the overflow flag
 *		Rc		Predicate to record CR0
 **/

template< class RA, class RB, class RC, class CA, class OE, class Rc >
void powerpc_cpu::execute_addition(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 c = RC::get(this, opcode);
	uint32 d = a + b + c;

	// Set XER (CA) if instruction affects carry bit
	if (CA::test(opcode))
		xer().set_ca(op_carry<op_add>::apply(a, b, c));

	// Set XER (OV, SO) if instruction has OE set
	if (OE::test(opcode))
		xer().set_ov(op_overflow<op_add>::apply(a, b, c));

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// Commit result to output operand
	operand_RD::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Generic arithmetic instruction
 *
 *		OP		Operation to perform
 *		RD		Output register
 *		RA		Input operand register
 *		RB		Input operand register or immediate (optional: operand_NONE)
 *		RC		Input operand register or immediate (optional: operand_NONE)
 *		OE		Predicate to compute overflow flag
 *		Rc		Predicate to record CR0
 **/

template< class OP, class RD, class RA, class RB, class RC, class OE, class Rc >
void powerpc_cpu::execute_generic_arith(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 c = RC::get(this, opcode);

	uint32 d = op_apply<uint32, OP, RA, RB, RC>::apply(a, b, c);

	// Set XER (OV, SO) if instruction has OE set
	if (OE::test(opcode))
		xer().set_ov(op_overflow<OP>::apply(a, b, c));

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// commit result to output operand
	RD::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Rotate Left Word Immediate then Mask Insert
 *
 *		SH		Shift count
 *		MA		Mask value
 *		Rc		Predicate to record CR0
 **/

template< class SH, class MA, class Rc >
void powerpc_cpu::execute_rlwimi(uint32 opcode)
{
	const uint32 n = SH::get(this, opcode);
	const uint32 m = MA::get(this, opcode);
	const uint32 rs = operand_RS::get(this, opcode);
	const uint32 ra = operand_RA::get(this, opcode);
	uint32 d = op_ppc_rlwimi::apply(rs, n, m, ra);

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// Commit result to output operand
	operand_RA::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Shift instructions
 *
 *		OP		Operation to perform
 *		RD		Output operand
 *		RA		Source operand
 *		SH		Shift count
 *		SO		Shift operation
 *		CA		Predicate to compute carry bit
 *		Rc		Predicate to record CR0
 **/

template< class OP >
struct invalid_shift {
	static inline uint32 value(uint32) {
		return 0;
	}
};

template<>
struct invalid_shift<op_shra> {
	static inline uint32 value(uint32 r) {
		return 0 - (r >> 31);
	}
};

template< class OP, class RD, class RA, class SH, class SO, class CA, class Rc >
void powerpc_cpu::execute_shift(uint32 opcode)
{
	const uint32 n = SO::apply(SH::get(this, opcode));
	const uint32 r = RA::get(this, opcode);
	uint32 d;

	// Shift operation is valid only if rB[26] = 0
	if (n & 0x20) {
		d = invalid_shift<OP>::value(r);
		if (CA::test(opcode))
			xer().set_ca(d >> 31);
	}
	else {
		d = OP::apply(r, n);
		if (CA::test(opcode)) {
			const uint32 ca = (r & 0x80000000) && (r & ~(0xffffffff << n));
			xer().set_ca(ca);
		}
	}

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// Commit result to output operand
	RD::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Branch conditional instructions
 *
 *		PC		Input program counter (PC, LR, CTR)
 *		BO		BO operand
 *		DP		Displacement operand
 *		AA		Predicate for absolute address
 *		LK		Predicate to record NPC into link register
 **/

template< class PC, class BO, class DP, class AA, class LK >
void powerpc_cpu::execute_branch(uint32 opcode)
{
	const int bo = BO::get(this, opcode);
	bool ctr_ok = true;
	bool cond_ok = true;

	if (BO_CONDITIONAL_BRANCH(bo)) {
		cond_ok = cr().test(BI_field::extract(opcode));
		if (!BO_BRANCH_IF_TRUE(bo))
			cond_ok = !cond_ok;
	}

	if (BO_DECREMENT_CTR(bo)) {
		ctr_ok = (ctr() -= 1) == 0;
		if (!BO_BRANCH_IF_CTR_ZERO(bo))
			ctr_ok = !ctr_ok;
	}

	const uint32 npc = pc() + 4;
	if (ctr_ok && cond_ok)
		pc() = ((AA::test(opcode) ? 0 : PC::get(this, opcode)) + DP::get(this, opcode)) & -4;
	else
		pc() = npc;

	if (LK::test(opcode))
		lr() = npc;
}

/**
 *	Compare instructions
 *
 *		RB		Second operand (GPR, SIMM, UIMM)
 *		CT		Type of variables to be compared (uint32, int32)
 **/

template< class RB, typename CT >
void powerpc_cpu::execute_compare(uint32 opcode)
{
	const uint32 a = operand_RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 crfd = crfD_field::extract(opcode);
	record_cr(crfd, (CT)a < (CT)b ? -1 : ((CT)a > (CT)b ? +1 : 0));
	increment_pc(4);
}

/**
 *	Operations on condition register
 *
 *		OP		Operation to perform
 **/

template< class OP >
void powerpc_cpu::execute_cr_op(uint32 opcode)
{
	const uint32 crbA = crbA_field::extract(opcode);
	uint32 a = (cr().get() >> (31 - crbA)) & 1;
	const uint32 crbB = crbB_field::extract(opcode);
	uint32 b = (cr().get() >> (31 - crbB)) & 1;
	const uint32 crbD = crbD_field::extract(opcode);
	uint32 d = OP::apply(a, b) & 1;
	cr().set((cr().get() & ~(1 << (31 - crbD))) | (d << (31 - crbD)));
	increment_pc(4);
}

/**
 *	Divide instructions
 *
 *		SB		Signed division
 *		OE		Predicate to compute overflow
 *		Rc		Predicate to record CR0
 **/

template< bool SB, class OE, class Rc >
void powerpc_cpu::execute_divide(uint32 opcode)
{
	const uint32 a = operand_RA::get(this, opcode);
	const uint32 b = operand_RB::get(this, opcode);
	uint32 d;

	// Specialize divide semantic action
	if (OE::test(opcode))
		d = do_execute_divide<SB, true>(a, b);
	else
		d = do_execute_divide<SB, false>(a, b);

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((int32)d);

	// Commit result to output operand
	operand_RD::set(this, opcode, d);

	increment_pc(4);
}

/**
 *	Multiply instructions
 *
 *		HI		Predicate for multiply high word
 *		SB		Predicate for signed operation
 *		OE		Predicate to compute overflow
 *		Rc		Predicate to record CR0
 **/

template< bool HI, bool SB, class OE, class Rc >
void powerpc_cpu::execute_multiply(uint32 opcode)
{
	const uint32 a = operand_RA::get(this, opcode);
	const uint32 b = operand_RB::get(this, opcode);
	uint64 d = SB ? (int64)(int32)a * (int64)(int32)b : (uint64)a * (uint64)b;

	// Overflow if the product cannot be represented in 32 bits
	if (OE::test(opcode)) {
		xer().set_ov((d & UVAL64(0xffffffff80000000)) != 0 &&
					 (d & UVAL64(0xffffffff80000000)) != UVAL64(0xffffffff80000000));
	}

	// Only keep high word if multiply high instruction
	if (HI)
		d >>= 32;

	// Set CR0 (LT, GT, EQ, SO) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr0((uint32)d);

	// Commit result to output operand
	operand_RD::set(this, opcode, (uint32)d);

	increment_pc(4);
}

/**
 *  Record FPSCR
 *
 *		Update FP exception bits
 **/

void powerpc_cpu::record_fpscr(int exceptions)
{
#if PPC_ENABLE_FPU_EXCEPTIONS
	// Reset non-sticky bits
	fpscr() &= ~(FPSCR_VX_field::mask() | FPSCR_FEX_field::mask());

	// Always update FX if any exception bit was set
	if (exceptions)
		fpscr() |= FPSCR_FX_field::mask() | exceptions;

	// Always update VX
	if (fpscr() & (FPSCR_VXSNAN_field::mask() | FPSCR_VXISI_field::mask() |
				   FPSCR_VXISI_field::mask() | FPSCR_VXIDI_field::mask() |
				   FPSCR_VXZDZ_field::mask() | FPSCR_VXIMZ_field::mask() |
				   FPSCR_VXVC_field::mask() | FPSCR_VXSOFT_field::mask() |
				   FPSCR_VXSQRT_field::mask() | FPSCR_VXCVI_field::mask()))
		fpscr() |= FPSCR_VX_field::mask();

	// Always update FEX
	if (((fpscr() & FPSCR_VX_field::mask()) && (fpscr() & FPSCR_VE_field::mask())) ||
		((fpscr() & FPSCR_OX_field::mask()) && (fpscr() & FPSCR_OE_field::mask())) ||
		((fpscr() & FPSCR_UX_field::mask()) && (fpscr() & FPSCR_UE_field::mask())) ||
		((fpscr() & FPSCR_ZX_field::mask()) && (fpscr() & FPSCR_ZE_field::mask())) ||
		((fpscr() & FPSCR_XX_field::mask()) && (fpscr() & FPSCR_XE_field::mask())))
		fpscr() |= FPSCR_FEX_field::mask();
#endif
}

/**
 *	Floating-point arithmetics
 *
 *		FP		Floating Point type
 *		OP		Operation to perform
 *		RD		Output register
 *		RA		Input operand
 *		RB		Input operand (optional)
 *		RC		Input operand (optional)
 *		Rc		Predicate to record CR1
 *		FPSCR	Predicate to compute FPSCR bits
 **/

template< class FP, class OP, class RD, class RA, class RB, class RC, class Rc, bool FPSCR >
void powerpc_cpu::execute_fp_arith(uint32 opcode)
{
	const double a = RA::get(this, opcode);
	const double b = RB::get(this, opcode);
	const double c = RC::get(this, opcode);

#if PPC_ENABLE_FPU_EXCEPTIONS
	int exceptions;
	if (FPSCR) {
		exceptions = op_apply<uint32, fp_exception_condition<OP>, RA, RB, RC>::apply(a, b, c);
		feclearexcept(FE_ALL_EXCEPT);
		febarrier();
	}
#endif

	FP d = op_apply<double, OP, RA, RB, RC>::apply(a, b, c);

	if (FPSCR) {

		// Update FPSCR exception bits
#if PPC_ENABLE_FPU_EXCEPTIONS
		febarrier();
		int raised = fetestexcept(FE_ALL_EXCEPT);
		if (raised & FE_INEXACT)
			exceptions |= FPSCR_XX_field::mask();
		if (raised & FE_DIVBYZERO)
			exceptions |= FPSCR_ZX_field::mask();
		if (raised & FE_UNDERFLOW)
			exceptions |= FPSCR_UX_field::mask();
		if (raised & FE_OVERFLOW)
			exceptions |= FPSCR_OX_field::mask();
		record_fpscr(exceptions);
#endif

		// FPSCR[FPRF] is set to the class and sign of the result
		if (!FPSCR_VE_field::test(fpscr()))
			fp_classify(d);
	}
	
	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	// Commit result to output operand
	RD::set(this, opcode, d);
	increment_pc(4);
}

/**
 *	Load/store instructions
 *
 *		OP		Operation to perform on loaded value
 *		RA		Base operand
 *		RB		Displacement (GPR(RB), EXTS(d))
 *		LD		Load operation?
 *		SZ		Size of load/store operation
 *		UP		Update RA with EA
 *		RX		Reverse operand
 **/

template< int SZ, bool RX >
struct memory_helper;

#define DEFINE_MEMORY_HELPER(SIZE)																\
template< bool RX >																				\
struct memory_helper<SIZE, RX>																	\
{																								\
	static inline uint32 load(uint32 ea) {														\
		return RX ? vm_read_memory_##SIZE##_reversed(ea) : vm_read_memory_##SIZE(ea);			\
	}																							\
	static inline void store(uint32 ea, uint32 value) {											\
		RX ? vm_write_memory_##SIZE##_reversed(ea, value) : vm_write_memory_##SIZE(ea, value);	\
	}																							\
}

DEFINE_MEMORY_HELPER(1);
DEFINE_MEMORY_HELPER(2);
DEFINE_MEMORY_HELPER(4);

template< class OP, class RA, class RB, bool LD, int SZ, bool UP, bool RX >
void powerpc_cpu::execute_loadstore(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 ea = a + b;

	if (LD)
		operand_RD::set(this, opcode, OP::apply(memory_helper<SZ, RX>::load(ea)));
	else {
#ifdef SHEEPSHAVER
		if (SZ == 4 && !RX) {
			cycle14_watchpoint(pc(), lr(), ea, operand_RS::get(this, opcode));
		}
#endif
		memory_helper<SZ, RX>::store(ea, operand_RS::get(this, opcode));
	}

	if (UP)
		RA::set(this, opcode, ea);

	increment_pc(4);
}

template< class RA, class DP, bool LD >
void powerpc_cpu::execute_loadstore_multiple(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 d = DP::get(this, opcode);
	uint32 ea = a + d;
/*
	// FIXME: generate exception if ea is not word-aligned
	if ((ea & 3) != 0) {
#ifdef SHEEPSHAVER
		D(bug("unaligned load/store multiple to %08x\n", ea));
		increment_pc(4);
		return;
#else
		abort();
#endif
	}
*/
	int r = LD ? rD_field::extract(opcode) : rS_field::extract(opcode);
	while (r <= 31) {
		if (LD)
			gpr(r) = vm_read_memory_4(ea);
		else {
#ifdef SHEEPSHAVER
			cycle14_watchpoint(pc(), lr(), ea, gpr(r));
#endif
			vm_write_memory_4(ea, gpr(r));
		}
		r++;
		ea += 4;
	}

	increment_pc(4);
}

/**
 *	Floating-point load/store instructions
 *
 *		RA		Base operand
 *		RB		Displacement (GPR(RB), EXTS(d))
 *		LD		Load operation?
 *		DB		Predicate for double value
 *		UP		Predicate to update RA with EA
 **/

template< class RA, class RB, bool LD, bool DB, bool UP >
void powerpc_cpu::execute_fp_loadstore(uint32 opcode)
{
	const uint32 a = RA::get(this, opcode);
	const uint32 b = RB::get(this, opcode);
	const uint32 ea = a + b;
	uint64 v;

	if (LD) {
		if (DB)
			v = vm_read_memory_8(ea);
		else
			v = fp_load_single_convert(vm_read_memory_4(ea));
		operand_fp_dw_RD::set(this, opcode, v);
	}
	else {
		v = operand_fp_dw_RS::get(this, opcode);
		if (DB)
			vm_write_memory_8(ea, v);
		else
			vm_write_memory_4(ea, fp_store_single_convert(v));
	}

	if (UP)
		RA::set(this, opcode, ea);

	increment_pc(4);
}

/**
 *	Load/Store String Word instruction
 *
 *		RA		Input operand as base EA
 *		IM		lswi mode?
 *		NB		Number of bytes to transfer
 **/

template< class RA, bool IM, class NB >
void powerpc_cpu::execute_load_string(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode);
	if (!IM)
		ea += operand_RB::get(this, opcode);

	int nb = NB::get(this, opcode);
	if (IM && nb == 0)
		nb = 32;

	int rd = rD_field::extract(opcode);
#if 1
	int i;
	for (i = 0; nb - i >= 4; i += 4, rd = (rd + 1) & 0x1f)
		gpr(rd) = vm_read_memory_4(ea + i);
	switch (nb - i) {
	case 1:
		gpr(rd) = vm_read_memory_1(ea + i) << 24;
		break;
	case 2:
		gpr(rd) = vm_read_memory_2(ea + i) << 16;
		break;
	case 3:
		gpr(rd) = (vm_read_memory_2(ea + i) << 16) + (vm_read_memory_1(ea + i + 2) << 8);
		break;
	}
#else
	for (int i = 0; i < nb; i++) {
		switch (i & 3) {
		case 0:
			gpr(rd) = vm_read_memory_1(ea + i) << 24;
			break;
		case 1:
			gpr(rd) = (gpr(rd) & 0xff00ffff) | (vm_read_memory_1(ea + i) << 16);
			break;
		case 2:
			gpr(rd) = (gpr(rd) & 0xffff00ff) | (vm_read_memory_1(ea + i) << 8);
			break;
		case 3:
			gpr(rd) = (gpr(rd) & 0xffffff00) | vm_read_memory_1(ea + i);
			rd = (rd + 1) & 0x1f;
			break;
		}
	}
#endif

	increment_pc(4);
}

template< class RA, bool IM, class NB >
void powerpc_cpu::execute_store_string(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode);
	if (!IM)
		ea += operand_RB::get(this, opcode);

	int nb = NB::get(this, opcode);
	if (IM && nb == 0)
		nb = 32;

	int rs = rS_field::extract(opcode);
	int sh = 24;
	for (int i = 0; i < nb; i++) {
		vm_write_memory_1(ea + i, gpr(rs) >> sh);
		sh -= 8;
		if (sh < 0) {
			sh = 24;
			rs = (rs + 1) & 0x1f;
		}
	}

	increment_pc(4);
}

/**
 *	Load Word and Reserve Indexed / Store Word Conditional Indexed
 *
 *		RA		Input operand as base EA
 **/

template< class RA >
void powerpc_cpu::execute_lwarx(uint32 opcode)
{
	const uint32 ea = RA::get(this, opcode) + operand_RB::get(this, opcode);
	uint32 reserve_data = vm_read_memory_4(ea);
	regs().reserve_valid = 1;
	regs().reserve_addr = ea;
#if KPX_MAX_CPUS != 1
	regs().reserve_data = reserve_data;
#endif
	operand_RD::set(this, opcode, reserve_data);
	increment_pc(4);
}

template< class RA >
void powerpc_cpu::execute_stwcx(uint32 opcode)
{
	const uint32 ea = RA::get(this, opcode) + operand_RB::get(this, opcode);
	cr().clear(0);
	if (regs().reserve_valid) {
		if (regs().reserve_addr == ea /* physical_addr(EA) */
#if KPX_MAX_CPUS != 1
			/* HACK: if another processor wrote to the reserved block,
			   nothing happens, i.e. we should operate as if reserve == 0 */
			&& regs().reserve_data == vm_read_memory_4(ea)
#endif
			) {
#ifdef SHEEPSHAVER
			cycle14_watchpoint(pc(), lr(), ea, operand_RS::get(this, opcode));
#endif
			vm_write_memory_4(ea, operand_RS::get(this, opcode));
			cr().set(0, standalone_CR_EQ_field::mask());
		}
		regs().reserve_valid = 0;
	}
	cr().set_so(0, xer().get_so());
	increment_pc(4);
}

/**
 *	Floating-point compare instruction
 *
 *		OC		Predicate for ordered compare
 **/

template< bool OC >
void powerpc_cpu::execute_fp_compare(uint32 opcode)
{
	const double a = operand_fp_RA::get(this, opcode);
	const double b = operand_fp_RB::get(this, opcode);
	const int crfd = crfD_field::extract(opcode);
	int c;

	if (is_NaN(a) || is_NaN(b))
		c = 1;
	else if (isless(a, b))
		c = 8;
	else if (isgreater(a, b))
		c = 4;
	else
		c = 2;

	FPSCR_FPCC_field::insert(fpscr(), c);
	cr().set(crfd, c);

	// Update FPSCR exception bits
#if PPC_ENABLE_FPU_EXCEPTIONS
	int exceptions = 0;
	if (is_SNaN(a) || is_SNaN(b)) {
		exceptions |= FPSCR_VXSNAN_field::mask();
		if (OC && !FPSCR_VE_field::test(fpscr()))
			exceptions |= FPSCR_VXVC_field::mask();
	}
	else if (OC && (is_QNaN(a) || is_QNaN(b)))
		exceptions |= FPSCR_VXVC_field::mask();
	record_fpscr(exceptions);
#endif

	increment_pc(4);
}

/**
 *	Floating Convert to Integer Word instructions
 *
 *		RN		Rounding mode
 *		Rc		Predicate to record CR1
 **/

template< class RN, class Rc >
void powerpc_cpu::execute_fp_int_convert(uint32 opcode)
{
	const double b = operand_fp_RB::get(this, opcode);
	const uint32 r = RN::get(this, opcode);
	any_register d;

#if PPC_ENABLE_FPU_EXCEPTIONS
	int exceptions = 0;
	if (is_NaN(b)) {
		exceptions |= FPSCR_VXCVI_field::mask();
		if (is_SNaN(b))
			exceptions |= FPSCR_VXSNAN_field::mask();
	}
	if (isinf(b))
		exceptions |= FPSCR_VXCVI_field::mask();

	feclearexcept(FE_ALL_EXCEPT);
	febarrier();
#endif

	// Convert to integer word if operand fits bounds
	if (b >= -(double)0x80000000 && b <= (double)0x7fffffff) {
#if defined mathlib_lrint
		int old_round = fegetround();
		fesetround(ppc_to_native_rounding_mode(r));
		d.j = (int32)mathlib_lrint(b);
		fesetround(old_round);
#else
		switch (r) {
		case 0: d.j = (int32)op_frin::apply(b); break; // near
		case 1: d.j = (int32)op_friz::apply(b); break; // zero
		case 2: d.j = (int32)op_frip::apply(b); break; // +inf
		case 3: d.j = (int32)op_frim::apply(b); break; // -inf
		}
#endif
	}

	// NOTE: this catches infinity and NaN operands
	else if (b > 0)
		d.j = 0x7fffffff;
	else
		d.j = 0x80000000;

	// Update FPSCR exception bits
#if PPC_ENABLE_FPU_EXCEPTIONS
	febarrier();
	int raised = fetestexcept(FE_ALL_EXCEPT);
	if (raised & FE_UNDERFLOW)
		exceptions |= FPSCR_UX_field::mask();
	if (raised & FE_INEXACT)
		exceptions |= FPSCR_XX_field::mask();
	record_fpscr(exceptions);
#endif

	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	// Commit result to output operand
	operand_fp_RD::set(this, opcode, d.d);
	increment_pc(4);
}

/**
 *	Floating-point Round to Single
 *
 *		Rc		Predicate to record CR1
 **/

#ifndef FPCLASSIFY_RETURN_T
#ifdef __MINGW32__
#define FPCLASSIFY_RETURN_T int
#else
#define FPCLASSIFY_RETURN_T uint8
#endif
#endif

template< class FP >
void powerpc_cpu::fp_classify(FP x)
{
	uint32 c = fpscr() & ~FPSCR_FPRF_field::mask();
	FPCLASSIFY_RETURN_T fc = fpclassify(x);
	switch (fc) {
	case FP_NAN:
		c |= FPSCR_FPRF_FU_field::mask() | FPSCR_FPRF_C_field::mask();
		break;
	case FP_ZERO:
		c |= FPSCR_FPRF_FE_field::mask();
		if (signbit(x))
			c |= FPSCR_FPRF_C_field::mask();
		break;
	case FP_INFINITE:
		c |= FPSCR_FPRF_FU_field::mask();
		goto FL_FG_field;
	case FP_SUBNORMAL:
		c |= FPSCR_FPRF_C_field::mask();
		// fall-through
	case FP_NORMAL:
	  FL_FG_field:
		if (x < 0)
			c |= FPSCR_FPRF_FL_field::mask();
		else
			c |= FPSCR_FPRF_FG_field::mask();
		break;
	}
	fpscr() = c;
}

template< class Rc >
void powerpc_cpu::execute_fp_round(uint32 opcode)
{
	const double b = operand_fp_RB::get(this, opcode);

#if PPC_ENABLE_FPU_EXCEPTIONS
	int exceptions =
		fp_invalid_operation_condition<double>::
		apply(FPSCR_VXSNAN_field::mask(), b);

	feclearexcept(FE_ALL_EXCEPT);
	febarrier();
#endif

	float d = (float)b;

	// Update FPSCR exception bits
#if PPC_ENABLE_FPU_EXCEPTIONS
	febarrier();
	int raised = fetestexcept(FE_ALL_EXCEPT);
	if (raised & FE_UNDERFLOW)
		exceptions |= FPSCR_UX_field::mask();
	if (raised & FE_OVERFLOW)
		exceptions |= FPSCR_OX_field::mask();
	if (raised & FE_INEXACT)
		exceptions |= FPSCR_XX_field::mask();
	record_fpscr(exceptions);
#endif

	// FPSCR[FPRF] is set to the class and sign of the result
	if (!FPSCR_VE_field::test(fpscr()))
		fp_classify(d);

	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	// Commit result to output operand
	operand_fp_RD::set(this, opcode, (double)d);
	increment_pc(4);
}

/**
 *		System Call instruction
 **/

void powerpc_cpu::execute_syscall(uint32 opcode)
{
#ifdef SHEEPSHAVER
	execute_illegal(opcode);
#else
	cr().set_so(0, execute_do_syscall && !execute_do_syscall(this));
#endif
	increment_pc(4);
}

/**
 *		Instructions dealing with system registers
 **/

void powerpc_cpu::execute_mcrf(uint32 opcode)
{
	const int crfS = crfS_field::extract(opcode);
	const int crfD = crfD_field::extract(opcode);
	cr().set(crfD, cr().get(crfS));
	increment_pc(4);
}

void powerpc_cpu::execute_mcrfs(uint32 opcode)
{
	const int crfS = crfS_field::extract(opcode);
	const int crfD = crfD_field::extract(opcode);

	// The contents of FPSCR field crfS are copied to CR field crfD
	const uint32 m = 0xf << (28 - 4 * crfS);
	cr().set(crfD, (fpscr() & m) >> (28 - 4 * crfS));

	// All exception bits copied (except FEX and VX) are cleared in the FPSCR
	fpscr() &= ~(m & (FPSCR_FX_field::mask() | FPSCR_OX_field::mask() |
					  FPSCR_UX_field::mask() | FPSCR_ZX_field::mask() |
					  FPSCR_XX_field::mask() | FPSCR_VXSNAN_field::mask() |
					  FPSCR_VXISI_field::mask() | FPSCR_VXIDI_field::mask() |
					  FPSCR_VXZDZ_field::mask() | FPSCR_VXIMZ_field::mask() |
					  FPSCR_VXVC_field::mask() | FPSCR_VXSOFT_field::mask() |
					  FPSCR_VXSQRT_field::mask() | FPSCR_VXCVI_field::mask()));

	increment_pc(4);
}

void powerpc_cpu::execute_mcrxr(uint32 opcode)
{
	const int crfD = crfD_field::extract(opcode);
	const uint32 x = xer().get();
	cr().set(crfD, x >> 28);
	xer().set(x & 0x0fffffff);
	increment_pc(4);
}

void powerpc_cpu::execute_mtcrf(uint32 opcode)
{
	uint32 mask = field2mask[CRM_field::extract(opcode)];
	cr().set((operand_RS::get(this, opcode) & mask) | (cr().get() & ~mask));
	increment_pc(4);
}

template< class FM, class RB, class Rc >
void powerpc_cpu::execute_mtfsf(uint32 opcode)
{
	const uint64 fsf = RB::get(this, opcode);
	const uint32 f = FM::get(this, opcode);
	uint32 m = field2mask[f];

	// FPSCR[FX] is altered only if FM[0] = 1
	if ((f & 0x80) == 0)
		m &= ~FPSCR_FX_field::mask();

	// The mtfsf instruction cannot alter FPSCR[FEX] nor FPSCR[VX] explicitly
	int exceptions = fsf & m;
	exceptions &= ~(FPSCR_FEX_field::mask() | FPSCR_VX_field::mask());

	// Move frB bits to FPSCR according to field mask
	fpscr() = (fpscr() & ~m) | exceptions;

	// Update FPSCR exception bits (don't implicitly update FX)
	record_fpscr(0);

	// Update native FP control word
	if (m & FPSCR_RN_field::mask())
		fesetround(ppc_to_native_rounding_mode(FPSCR_RN_field::extract(fpscr())));

	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	increment_pc(4);
}

template< class RB, class Rc >
void powerpc_cpu::execute_mtfsfi(uint32 opcode)
{
	const uint32 crfD = crfD_field::extract(opcode);
	uint32 m = 0xf << (4 * (7 - crfD));

	// FPSCR[FX] is altered only if crfD = 0
	if (crfD == 0)
		m &= ~FPSCR_FX_field::mask();

	// The mtfsfi instruction cannot alter FPSCR[FEX] nor FPSCR[VX] explicitly
	int exceptions = RB::get(this, opcode) & m;
	exceptions &= ~(FPSCR_FEX_field::mask() | FPSCR_VX_field::mask());

	// Move immediate to FPSCR according to field crfD
	fpscr() = (fpscr() & ~m) | exceptions;

	// Update native FP control word
	if (m & FPSCR_RN_field::mask())
		fesetround(ppc_to_native_rounding_mode(FPSCR_RN_field::extract(fpscr())));

	// Update FPSCR exception bits (don't implicitly update FX)
	record_fpscr(0);

	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	increment_pc(4);
}

template< class RB, class Rc >
void powerpc_cpu::execute_mtfsb(uint32 opcode)
{
	const bool set_bit = RB::get(this, opcode);

	// The mtfsb0 and mtfsb1 instructions cannot alter FPSCR[FEX] nor FPSCR[VX] explicitly
	uint32 m = 1 << (31 - crbD_field::extract(opcode));
	m &= ~(FPSCR_FEX_field::mask() | FPSCR_VX_field::mask());

	// Bit crbD of the FPSCR is set or clear
	fpscr() &= ~m;

	// Update FPSCR exception bits
	record_fpscr(set_bit ? m : 0);

	// Update native FP control word if FPSCR[RN] changed
	if (m & FPSCR_RN_field::mask())
		fesetround(ppc_to_native_rounding_mode(FPSCR_RN_field::extract(fpscr())));

	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	increment_pc(4);
}

template< class Rc >
void powerpc_cpu::execute_mffs(uint32 opcode)
{
	// Move FPSCR to FPR(FRD)
	operand_fp_dw_RD::set(this, opcode, fpscr());

	// Set CR1 (FX, FEX, VX, VOX) if instruction has Rc set
	if (Rc::test(opcode))
		record_cr1();

	increment_pc(4);
}

void powerpc_cpu::execute_mfmsr(uint32 opcode)
{
	operand_RD::set(this, opcode, 0xf072);
	increment_pc(4);
}

template< class SPR >
void powerpc_cpu::execute_mfspr(uint32 opcode)
{
	const uint32 spr = SPR::get(this, opcode);
	uint32 d;
	switch (spr) {
	case powerpc_registers::SPR_XER:	d = xer().get();break;
	case powerpc_registers::SPR_LR:		d = lr();		break;
	case powerpc_registers::SPR_CTR:	d = ctr();		break;
	case powerpc_registers::SPR_VRSAVE:	d = vrsave();	break;
#ifdef SHEEPSHAVER
	case powerpc_registers::SPR_SDR1:	d = 0xdead001f;	break;
	case powerpc_registers::SPR_PVR: {
		extern uint32 PVR;
		d = PVR;
		break;
	}
	default: d = 0;
#else
	default: execute_illegal(opcode);
#endif
	}
	operand_RD::set(this, opcode, d);
	increment_pc(4);
}

template< class SPR >
void powerpc_cpu::execute_mtspr(uint32 opcode)
{
	const uint32 spr = SPR::get(this, opcode);
	const uint32 s = operand_RS::get(this, opcode);

	switch (spr) {
	case powerpc_registers::SPR_XER:	xer().set(s);	break;
	case powerpc_registers::SPR_LR:		lr() = s;		break;
	case powerpc_registers::SPR_CTR:	ctr() = s;		break;
	case powerpc_registers::SPR_VRSAVE:	vrsave() = s;	break;
#ifndef SHEEPSHAVER
	default: execute_illegal(opcode);
#endif
	}

	increment_pc(4);
}

// Compute with 96 bit intermediate result: (a * b) / c
static uint64 muldiv64(uint64 a, uint32 b, uint32 c)
{
	union {
		uint64 ll;
		struct {
#ifdef WORDS_BIGENDIAN
			uint32 high, low;
#else
			uint32 low, high;
#endif
		} l;
	} u, res;

	u.ll = a;
	uint64 rl = (uint64)u.l.low * (uint64)b;
	uint64 rh = (uint64)u.l.high * (uint64)b;
	rh += (rl >> 32);
	res.l.high = rh / c;
	res.l.low = (((rh % c) << 32) + (rl & 0xffffffff)) / c;
	return res.ll;
}

static inline uint64 get_tb_ticks(void)
{
	uint64 ticks;
#ifdef SHEEPSHAVER
	const uint32 TBFreq = TimebaseSpeed;
	ticks = muldiv64(GetTicks_usec(), TBFreq, 1000000);
#else
	const uint32 TBFreq = 25 * 1000 * 1000; // 25 MHz
	ticks = muldiv64((uint64)clock(), TBFreq, CLOCKS_PER_SEC);
#endif
	return ticks;
}

template< class TBR >
void powerpc_cpu::execute_mftbr(uint32 opcode)
{
	uint32 tbr = TBR::get(this, opcode);
	uint32 d = 0;
	switch (tbr) {
	case 268: d = (uint32)get_tb_ticks(); break;
	case 269: d = (get_tb_ticks() >> 32); break;
	default: execute_illegal(opcode);
	}
	operand_RD::set(this, opcode, d);
	increment_pc(4);
}

/**
 *		Instruction cache management
 **/

void powerpc_cpu::execute_invalidate_cache_range()
{
	if (cache_range.start != cache_range.end) {
		invalidate_cache_range(cache_range.start, cache_range.end);
		cache_range.start = cache_range.end = 0;
	}
}

template< class RA, class RB >
void powerpc_cpu::execute_icbi(uint32 opcode)
{
	const uint32 ea = RA::get(this, opcode) + RB::get(this, opcode);
	const uint32 block_start = ea - (ea % 32);

	if (block_start == cache_range.end) {
		// Extend region to invalidate
		cache_range.end += 32;
	}
	else {
		// New region to invalidate
		execute_invalidate_cache_range();
		cache_range.start = block_start;
		cache_range.end = cache_range.start + 32;
	}

	increment_pc(4);
}

void powerpc_cpu::execute_isync(uint32 opcode)
{
	execute_invalidate_cache_range();
	increment_pc(4);
}

/**
 *		(Fake) data cache management
 **/

template< class RA, class RB >
void powerpc_cpu::execute_dcbz(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode) + RB::get(this, opcode);
	vm_memset(ea - (ea % 32), 0, 32);
	increment_pc(4);
}

/**
 *		Vector load/store instructions
 **/

template< bool SL >
void powerpc_cpu::execute_vector_load_for_shift(uint32 opcode)
{
	const uint32 ra = operand_RA_or_0::get(this, opcode);
	const uint32 rb = operand_RB::get(this, opcode);
	const uint32 ea = ra + rb;
	powerpc_vr & vD = vr(vD_field::extract(opcode));
	int j = SL ? (ea & 0xf) : (0x10 - (ea & 0xf));
	for (int i = 0; i < 16; i++)
		vD.b[ev_mixed::byte_element(i)] = j++;
	increment_pc(4);
}

template< class VD, class RA, class RB >
void powerpc_cpu::execute_vector_load(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode) + RB::get(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	switch (VD::element_size) {
	case 1:
		VD::set_element(vD, (ea & 0x0f), vm_read_memory_1(ea));
		break;
	case 2:
		VD::set_element(vD, ((ea >> 1) & 0x07), vm_read_memory_2(ea & ~1));
		break;
	case 4:
		VD::set_element(vD, ((ea >> 2) & 0x03), vm_read_memory_4(ea & ~3));
		break;
	case 8:
		ea &= ~15;
		vD.w[0] = vm_read_memory_4(ea +  0);
		vD.w[1] = vm_read_memory_4(ea +  4);
		vD.w[2] = vm_read_memory_4(ea +  8);
		vD.w[3] = vm_read_memory_4(ea + 12);
		break;
	}
	increment_pc(4);
}

template< class VS, class RA, class RB >
void powerpc_cpu::execute_vector_store(uint32 opcode)
{
	uint32 ea = RA::get(this, opcode) + RB::get(this, opcode);
	typename VS::type & vS = VS::ref(this, opcode);
	switch (VS::element_size) {
	case 1:
		vm_write_memory_1(ea, VS::get_element(vS, (ea & 0x0f)));
		break;
	case 2:
		vm_write_memory_2(ea & ~1, VS::get_element(vS, ((ea >> 1) & 0x07)));
		break;
	case 4:
		vm_write_memory_4(ea & ~3, VS::get_element(vS, ((ea >> 2) & 0x03)));
		break;
	case 8:
		ea &= ~15;
		vm_write_memory_4(ea +  0, vS.w[0]);
		vm_write_memory_4(ea +  4, vS.w[1]);
		vm_write_memory_4(ea +  8, vS.w[2]);
		vm_write_memory_4(ea + 12, vS.w[3]);
		break;
	}
	increment_pc(4);
}

/**
 *	Vector arithmetic
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 *		Rc		Predicate to record CR6
 *		C1		If recording CR6, do we check for '1' bits in vD?
 **/

template< class OP, class VD, class VA, class VB, class VC, class Rc, int C1 >
void powerpc_cpu::execute_vector_arith(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VC::type const & vC = VC::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	for (int i = 0; i < n_elements; i++) {
		const typename VA::element_type a = VA::get_element(vA, i);
		const typename VB::element_type b = VB::get_element(vB, i);
		const typename VC::element_type c = VC::get_element(vC, i);
		typename VD::element_type d = op_apply<typename VD::element_type, OP, VA, VB, VC>::apply(a, b, c);
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, i, d);
	}

	// Propagate all conditions to CR6
	if (Rc::test(opcode))
		record_cr6(vD, C1);

	increment_pc(4);
}

/**
 *	Vector mixed arithmetic
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 **/

template< class OP, class VD, class VA, class VB, class VC >
void powerpc_cpu::execute_vector_arith_mixed(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VC::type const & vC = VC::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;
	const int n_sub_elements = 4 / VA::element_size;

	for (int i = 0; i < n_elements; i++) {
		const typename VC::element_type c = VC::get_element(vC, i);
		typename VD::element_type d = c;
		for (int j = 0; j < n_sub_elements; j++) {
			const typename VA::element_type a = VA::get_element(vA, i * n_sub_elements + j);
			const typename VB::element_type b = VB::get_element(vB, i * n_sub_elements + j);
			d += op_apply<typename VD::element_type, OP, VA, VB, null_vector_operand>::apply(a, b, c);
		}
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, i, d);
	}

	increment_pc(4);
}

/**
 *	Vector odd/even arithmetic
 *
 *		ODD		Flag: are we computing every odd element?
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 **/

template< int ODD, class OP, class VD, class VA, class VB, class VC >
void powerpc_cpu::execute_vector_arith_odd(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VC::type const & vC = VC::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	for (int i = 0; i < n_elements; i++) {
		const typename VA::element_type a = VA::get_element(vA, (i * 2) + ODD);
		const typename VB::element_type b = VB::get_element(vB, (i * 2) + ODD);
		const typename VC::element_type c = VC::get_element(vC, (i * 2) + ODD);
		typename VD::element_type d = op_apply<typename VD::element_type, OP, VA, VB, VC>::apply(a, b, c);
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, i, d);
	}

	increment_pc(4);
}

/**
 *	Vector merge instructions
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 *		LO		Flag: use lower part of element
 **/

template< class VD, class VA, class VB, int LO >
void powerpc_cpu::execute_vector_merge(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	for (int i = 0; i < n_elements; i += 2) {
		VD::set_element(vD, i    , VA::get_element(vA, (i / 2) + LO * (n_elements / 2)));
		VD::set_element(vD, i + 1, VB::get_element(vB, (i / 2) + LO * (n_elements / 2)));
	}

	increment_pc(4);
}

/**
 *	Vector pack/unpack instructions
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 *		LO		Flag: use lower part of element
 **/

template< class VD, class VA, class VB >
void powerpc_cpu::execute_vector_pack(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;
	const int n_pivot = n_elements / 2;

	for (int i = 0; i < n_elements; i++) {
		typename VD::element_type d;
		if (i < n_pivot)
			d = VA::get_element(vA, i);
		else
			d = VB::get_element(vB, i - n_pivot);
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, i, d);
	}

	increment_pc(4);
}

template< int LO, class VD, class VA >
void powerpc_cpu::execute_vector_unpack(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	for (int i = 0; i < n_elements; i++)
		VD::set_element(vD, i, VA::get_element(vA, i + LO * n_elements));

	increment_pc(4);
}

void powerpc_cpu::execute_vector_pack_pixel(uint32 opcode)
{
	powerpc_vr const & vA = vr(vA_field::extract(opcode));
	powerpc_vr const & vB = vr(vB_field::extract(opcode));
	powerpc_vr & vD = vr(vD_field::extract(opcode));

	for (int i = 0; i < 4; i++) {
		const uint32 a = vA.w[i];
		vD.h[ev_mixed::half_element(i)] = ((a >> 9) & 0xfc00) | ((a >> 6) & 0x03e0) | ((a >> 3) & 0x001f);
		const uint32 b = vB.w[i];
		vD.h[ev_mixed::half_element(i + 4)] = ((b >> 9) & 0xfc00) | ((b >> 6) & 0x03e0) | ((b >> 3) & 0x001f);
	}

	increment_pc(4);
}

template< int LO >
void powerpc_cpu::execute_vector_unpack_pixel(uint32 opcode)
{
	powerpc_vr const & vB = vr(vB_field::extract(opcode));
	powerpc_vr & vD = vr(vD_field::extract(opcode));

	for (int i = 0; i < 4; i++) {
		const uint32 h = vB.h[ev_mixed::half_element(i + LO * 4)];
		vD.w[i] = (((h & 0x8000) ? 0xff000000 : 0) |
				   ((h & 0x7c00) << 6) |
				   ((h & 0x03e0) << 3) |
				   (h & 0x001f));
	}

	increment_pc(4);
}

/**
 *	Vector shift instructions
 *
 *		SD		Shift direction: left (-1), right (+1)
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		VC		Input operand vector (optional: operand_NONE)
 *		SH		Shift count operand
 **/

template< int SD >
void powerpc_cpu::execute_vector_shift(uint32 opcode)
{
	powerpc_vr const & vA = vr(vA_field::extract(opcode));
	powerpc_vr const & vB = vr(vB_field::extract(opcode));
	powerpc_vr & vD = vr(vD_field::extract(opcode));

	// The contents of the low-order three bits of all byte
	// elements in vB must be identical to vB[125-127]; otherwise
	// the value placed into vD is undefined.
	const int sh = vB.b[ev_mixed::byte_element(15)] & 7;
	if (sh == 0) {
		for (int i = 0; i < 4; i++)
			vD.w[i] = vA.w[i];
	}
	else {
		uint32 prev_bits = 0;
		if (SD < 0) {
			for (int i = 3; i >= 0; i--) {
				uint32 next_bits = vA.w[i] >> (32 - sh);
				vD.w[i] = ((vA.w[i] << sh) | prev_bits);
				prev_bits = next_bits;
			}
		}
		else if (SD > 0) {
			for (int i = 0; i < 4; i++) {
				uint32 next_bits = vA.w[i] << (32 - sh);
				vD.w[i] = ((vA.w[i] >> sh) | prev_bits);
				prev_bits = next_bits;
			}
		}
	}

	increment_pc(4);
}

template< int SD, class VD, class VA, class VB, class SH >
void powerpc_cpu::execute_vector_shift_octet(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);

	const int sh = SH::get(this, opcode);
	if (SD < 0) {
		for (int i = 0; i < 16; i++) {
			if (i + sh < 16)
				VD::set_element(vD, i, VA::get_element(vA, i + sh));
			else
				VD::set_element(vD, i, VB::get_element(vB, i - (16 - sh)));
		}
	}
	else if (SD > 0) {
		for (int i = 0; i < 16; i++) {
			if (i < sh)
				VD::set_element(vD, i, VB::get_element(vB, 16 - (i - sh)));
			else
				VD::set_element(vD, i, VA::get_element(vA, i - sh));
		}
	}

	increment_pc(4);
}

/**
 *	Vector splat instructions
 *
 *		OP		Operation to perform on element
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 *		IM		Immediate value to replicate
 **/

template< class OP, class VD, class VB, bool IM >
void powerpc_cpu::execute_vector_splat(uint32 opcode)
{
	typename VD::type & vD = VD::ref(this, opcode);
	const int n_elements = 16 / VD::element_size;

	uint32 value;
	if (IM)
		value = OP::apply(vUIMM_field::extract(opcode));
	else {
		typename VB::type const & vB = VB::const_ref(this, opcode);
		const int n = vUIMM_field::extract(opcode) & (n_elements - 1);
		value = OP::apply(VB::get_element(vB, n));
	}

	for (int i = 0; i < n_elements; i++)
		VD::set_element(vD, i, value);

	increment_pc(4);
}

/**
 *	Vector sum instructions
 *
 *		SZ		Size of destination vector elements
 *		VD		Output operand vector
 *		VA		Input operand vector
 *		VB		Input operand vector (optional: operand_NONE)
 **/

template< int SZ, class VD, class VA, class VB >
void powerpc_cpu::execute_vector_sum(uint32 opcode)
{
	typename VA::type const & vA = VA::const_ref(this, opcode);
	typename VB::type const & vB = VB::const_ref(this, opcode);
	typename VD::type & vD = VD::ref(this, opcode);
	typename VD::element_type d;
	
	switch (SZ) {
	case 1: // vsum
		d = VB::get_element(vB, 3);
		for (int j = 0; j < 4; j++)
			d += VA::get_element(vA, j);
		if (VD::saturate(d))
			vscr().set_sat(1);
		VD::set_element(vD, 0, 0);
		VD::set_element(vD, 1, 0);
		VD::set_element(vD, 2, 0);
		VD::set_element(vD, 3, d);
		break;

	case 2: // vsum2
		for (int i = 0; i < 4; i += 2) {
			d = VB::get_element(vB, i + 1);
			for (int j = 0; j < 2; j++)
				d += VA::get_element(vA, i + j);
			if (VD::saturate(d))
				vscr().set_sat(1);
			VD::set_element(vD, i + 0, 0);
			VD::set_element(vD, i + 1, d);
		}
		break;

	case 4: // vsum4
		for (int i = 0; i < 4; i += 1) {
			d = VB::get_element(vB, i);
			const int n_elements = 4 / VA::element_size;
			for (int j = 0; j < n_elements; j++)
				d += VA::get_element(vA, i * n_elements + j);
			if (VD::saturate(d))
				vscr().set_sat(1);
			VD::set_element(vD, i, d);
		}
		break;
	}

	increment_pc(4);
}

/**
 *		Misc vector instructions
 **/

void powerpc_cpu::execute_vector_permute(uint32 opcode)
{
	powerpc_vr const & vA = vr(vA_field::extract(opcode));
	powerpc_vr const & vB = vr(vB_field::extract(opcode));
	powerpc_vr const & vC = vr(vC_field::extract(opcode));
	powerpc_vr & vD = vr(vD_field::extract(opcode));

	for (int i = 0; i < 16; i++) {
		const int ei = ev_mixed::byte_element(i);
		const int n  = vC.b[ei] & 0x1f;
		const int en = ev_mixed::byte_element(n & 0xf);
		vD.b[ei] = (n & 0x10) ? vB.b[en] : vA.b[en];
	}

	increment_pc(4);
}

void powerpc_cpu::execute_mfvscr(uint32 opcode)
{
	const int vD = vD_field::extract(opcode);
	vr(vD).w[0] = 0;
	vr(vD).w[1] = 0;
	vr(vD).w[2] = 0;
	vr(vD).w[3] = vscr().get();
	increment_pc(4);
}

void powerpc_cpu::execute_mtvscr(uint32 opcode)
{
	const int vB = vB_field::extract(opcode);
	vscr().set(vr(vB).w[3]);
	increment_pc(4);
}

/**
 *		Explicit template instantiations
 **/

#include "ppc-execute-impl.cpp"
