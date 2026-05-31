#!/usr/bin/env python3
#
#  extract_exports.py - Offline PEF/CFM loader-section export-table parser.
#
#  (C) 2026 Sierra Burkhart (sierra760)
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
#  Phase 22.5.1 Plan 22.5.1-01 (D-01/D-02/D-03). Host-side, OFFLINE, no ROM,
#  no device. Parses the classic PEF/CFM container loader section export hash
#  table of resources/DrawSprocketLib and emits the machine-readable export
#  manifest (dsp_export_set.json shape) that gates the install-table drift
#  detector. Reproducible in research + CI: stdlib-only (struct, hashlib,
#  json), no external packages (threat T-22.5.1-SC trivially satisfied).
#
#  Recipe (22.5.1-RESEARCH §"Decompile Tooling Recipe / Step 1"):
#    1. PEF Container Header @0 (40 bytes): tag1 "Joy!", tag2 "peff",
#       architecture "pwpc", sectionCount u16 @ +32.
#    2. Section Headers @40, stride 28: find sectionKind == 4 (Loader);
#       record its containerOffset (= L).
#    3. PEF Loader Info Header @L: loaderStringsOffset u32 @ L+40,
#       exportHashOffset u32 @ L+44, exportHashTablePower u32 @ L+48,
#       exportedSymbolCount u32 @ L+52.
#    4. nHash = 1 << exportHashTablePower.
#       hashSlotTable   @ L+exportHashOffset           (nHash u32 slots)
#       exportKeyTable  @ hashSlotTable + nHash*4       (count u32 hashwords;
#                          name length = (hashword >> 16) & 0xFFFF)
#       exportSymbolTable @ exportKeyTable + count*4    (count * 10 bytes:
#                          classAndName u32, symbolValue u32, sectionIndex i16)
#    5. name = strings[ classAndName & 0x00FFFFFF : +namelen ] decoded mac-roman.
#    6. Emit the SORTED export-name list as JSON.
#
#  Usage: python3 tools/dsp_pef/extract_exports.py [path-to-DrawSprocketLib]
#  (defaults to resources/DrawSprocketLib relative to the repo root).

import hashlib
import json
import os
import struct
import sys

PEF_CONTAINER_HEADER_SIZE = 40
PEF_SECTION_HEADER_SIZE = 28
PEF_LOADER_INFO_HEADER_SIZE = 56
PEF_EXPORT_SYMBOL_SIZE = 10
PEF_SECTION_KIND_LOADER = 4

DEFAULT_BINARY = "resources/DrawSprocketLib"
GENERATED_BY = "tools/dsp_pef/extract_exports.py"
SOURCE_BINARY = "resources/DrawSprocketLib"


def _fail(msg):
	# T-22.5.1-01: fail LOUDLY on any out-of-range / malformed structure
	# rather than silently emitting a short export list.
	sys.stderr.write("extract_exports.py: %s\n" % msg)
	sys.exit(2)


def _check_range(data, off, length, what):
	if off < 0 or length < 0 or off + length > len(data):
		_fail("%s out of range (offset=%d length=%d file=%d bytes)"
		      % (what, off, length, len(data)))


def parse_exports(data):
	# --- Step 1: PEF Container Header @0 -----------------------------------
	_check_range(data, 0, PEF_CONTAINER_HEADER_SIZE, "container header")
	tag1 = data[0:4]
	tag2 = data[4:8]
	arch = data[8:12]
	if tag1 != b"Joy!" or tag2 != b"peff" or arch != b"pwpc":
		_fail("not a PEF/pwpc container (magic=%r %r %r)" % (tag1, tag2, arch))
	# PEF Container Header: sectionCount (total) is a u16 @ +32
	# (+34 = instSectionCount, +36 = reservedA u32).
	(section_count,) = struct.unpack_from(">H", data, 32)
	if section_count == 0:
		_fail("section count is zero")

	# --- Step 2: Section Headers @40, stride 28 ----------------------------
	loader_off = None
	for i in range(section_count):
		hdr_off = PEF_CONTAINER_HEADER_SIZE + i * PEF_SECTION_HEADER_SIZE
		_check_range(data, hdr_off, PEF_SECTION_HEADER_SIZE,
		             "section header %d" % i)
		# nameOffset i32, defaultAddress u32, totalSize u32, unpackedSize u32,
		# packedSize u32, containerOffset u32, sectionKind u8, ...
		(container_off,) = struct.unpack_from(">I", data, hdr_off + 20)
		(section_kind,) = struct.unpack_from(">B", data, hdr_off + 24)
		if section_kind == PEF_SECTION_KIND_LOADER:
			loader_off = container_off
			break
	if loader_off is None:
		_fail("no Loader section (sectionKind==4) found")
	L = loader_off

	# --- Step 3: PEF Loader Info Header @L ---------------------------------
	_check_range(data, L, PEF_LOADER_INFO_HEADER_SIZE, "loader info header")
	(loader_strings_offset,) = struct.unpack_from(">I", data, L + 40)
	(export_hash_offset,) = struct.unpack_from(">I", data, L + 44)
	(export_hash_power,) = struct.unpack_from(">I", data, L + 48)
	(exported_symbol_count,) = struct.unpack_from(">I", data, L + 52)
	if export_hash_power > 31:
		_fail("implausible exportHashTablePower=%d" % export_hash_power)

	# --- Step 4: walk the hash-slot / key / symbol tables ------------------
	n_hash = 1 << export_hash_power
	hash_slot_table = L + export_hash_offset
	export_key_table = hash_slot_table + n_hash * 4
	export_symbol_table = export_key_table + exported_symbol_count * 4
	strings_base = L + loader_strings_offset

	_check_range(data, hash_slot_table, n_hash * 4, "export hash slot table")
	_check_range(data, export_key_table, exported_symbol_count * 4,
	             "export key table")
	_check_range(data, export_symbol_table,
	             exported_symbol_count * PEF_EXPORT_SYMBOL_SIZE,
	             "export symbol table")

	# Name length per export comes from the key-table hashword high 16 bits,
	# NOT a NUL terminator.
	name_lengths = []
	for i in range(exported_symbol_count):
		(hashword,) = struct.unpack_from(">I", data, export_key_table + i * 4)
		name_lengths.append((hashword >> 16) & 0xFFFF)

	# --- Step 5: resolve each export name ----------------------------------
	exports = []
	for i in range(exported_symbol_count):
		sym_off = export_symbol_table + i * PEF_EXPORT_SYMBOL_SIZE
		(class_and_name,) = struct.unpack_from(">I", data, sym_off + 0)
		# symbolValue u32 @ +4 and sectionIndex i16 @ +8 are not needed for the
		# name manifest (they feed routine disassembly in ppcdis.cpp).
		name_off = class_and_name & 0x00FFFFFF
		name_len = name_lengths[i]
		abs_off = strings_base + name_off
		_check_range(data, abs_off, name_len, "export name %d" % i)
		name = data[abs_off:abs_off + name_len].decode("mac-roman")
		exports.append(name)

	if len(exports) != exported_symbol_count:
		_fail("resolved %d names but header says %d"
		      % (len(exports), exported_symbol_count))

	# --- Step 6: sorted, deterministic --------------------------------------
	return exported_symbol_count, sorted(exports)


def main(argv):
	binary_path = argv[1] if len(argv) > 1 else DEFAULT_BINARY
	if not os.path.exists(binary_path):
		_fail("binary not found: %s" % binary_path)

	with open(binary_path, "rb") as f:
		data = f.read()

	count, exports = parse_exports(data)
	source_sha256 = hashlib.sha256(data).hexdigest()

	manifest = {
		"type": "pef_export_set",
		"source_binary": SOURCE_BINARY,
		"source_sha256": source_sha256,
		"exported_symbol_count": count,
		"exports": exports,
		"generated_by": GENERATED_BY,
		"notes": "Ground truth for the dsp_install_symbols[] drift gate (D-03). "
		         "Regenerate offline with: python3 %s %s"
		         % (GENERATED_BY, SOURCE_BINARY),
	}

	# 2-space indent + trailing newline: stable, human-diffable, deterministic.
	sys.stdout.write(json.dumps(manifest, indent=2, ensure_ascii=False))
	sys.stdout.write("\n")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
