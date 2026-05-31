/*
 *  ppcdis.cpp - standalone OFFLINE PowerPC disassembler driver.
 *
 *  (C) 2026 Sierra Burkhart (sierra760)
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
 *
 *  A ~20-line driver that wraps the
 *  in-repo cxmon word-decoder cxmon/src/mon_ppc.cpp::disass_ppc(FILE*, adr,
 *  word). NOT a new decoder. Reads 4-byte big-endian PPC instruction words
 *  from a committed reference binary at a given file offset and prints PPC
 *  mnemonics, fully OFFLINE — no ROM, no device, no emulator runtime.
 *
 *  Rationale: Apple's bundled objdump/LLVM has no PowerPC target;
 *  kpx_cpu is runtime-coupled; cxmon's disass_ppc is a pure word->text
 *  function — strictly the most reproducible offline option.
 *
 *  Build:
 *    clang++ -std=c++14 -include tools/dsp_pef/sysdeps_shim.h \
 *        -I tools/dsp_pef -I cxmon/src \
 *        -o ppcdis tools/dsp_pef/ppcdis.cpp cxmon/src/mon_ppc.cpp
 *
 *  Usage: ./ppcdis <binary> <file-offset-decimal> [word-count]
 *    e.g. ./ppcdis resources/DrawSprocketLib 160       (default 16 words)
 *         ./ppcdis resources/DrawSprocketLib 160 32
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mon_disass.h"

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr,
		        "usage: %s <binary> <file-offset> [word-count]\n",
		        argv[0]);
		return 2;
	}

	const char *path = argv[1];
	long offset = strtol(argv[2], NULL, 0);
	int word_count = (argc >= 4) ? (int)strtol(argv[3], NULL, 0) : 16;
	if (offset < 0 || word_count <= 0) {
		fprintf(stderr, "%s: bad offset/word-count\n", argv[0]);
		return 2;
	}

	FILE *bin = fopen(path, "rb");
	if (bin == NULL) {
		fprintf(stderr, "%s: cannot open %s\n", argv[0], path);
		return 2;
	}

	if (fseek(bin, offset, SEEK_SET) != 0) {
		fprintf(stderr, "%s: cannot seek to %ld\n", argv[0], offset);
		fclose(bin);
		return 2;
	}

	for (int i = 0; i < word_count; i++) {
		unsigned char raw[4];
		if (fread(raw, 1, 4, bin) != 4)
			break;
		/* PEF/CFM PowerPC code is big-endian. */
		unsigned int word = ((unsigned int)raw[0] << 24) |
		                    ((unsigned int)raw[1] << 16) |
		                    ((unsigned int)raw[2] << 8) |
		                    ((unsigned int)raw[3]);
		unsigned int addr = (unsigned int)(offset + i * 4);
		disass_ppc(stdout, addr, word);
	}

	fclose(bin);
	return 0;
}
