# `tools/dsp_pef/` — DrawSprocketLib offline decompile tooling

Host-side, **offline**, reproducible-in-CI tooling for extracting the
DrawSprocket 1.7 export-surface ground truth from the committed
`resources/DrawSprocketLib` PEF/CFM PowerPC binary. **No ROM, no device, no
emulator runtime** is required (Phase 22.5 decisions D-01 / D-02 / D-03).

These tools are dev/CI-only. They are **not** compiled into the PocketShaver
app and **never** sit on a runtime guest-input path. They parse a fixed,
committed reference binary supplied as a developer argv.

## Zero external packages (threat T-22.5.1-SC — trivially satisfied)

This directory installs **no** npm / pip / cargo / Homebrew packages.

- `extract_exports.py` uses **only** the Python standard library
  (`struct`, `hashlib`, `json`, `os`, `sys`).
- `ppcdis.cpp` links **only** the in-repo decoder `cxmon/src/mon_ppc.cpp`
  via the local `sysdeps_shim.h`.

There is therefore no package-legitimacy gate to clear — there is nothing
to install.

## Contents

| File | What it is |
|------|------------|
| `extract_exports.py` | Offline PEF loader-section export-hash-table parser. Emits the `dsp_export_set.json` manifest (53 real exports). |
| `ppcdis.cpp` | ~20-line standalone driver wrapping cxmon's `disass_ppc(FILE*, adr, word)` word-decoder. Disassembles a code offset offline. |
| `sysdeps_shim.h` | Minimal typedef shim (`uint32`/`uint16`/`uint8`/`int32`/`uintptr`) so cxmon's `mon.h`/`mon_ppc.cpp` compile without an autoconf `config.h`. |

## Reproduce 1 — extract the 53-export manifest

Run from the repo root:

```sh
python3 tools/dsp_pef/extract_exports.py resources/DrawSprocketLib
```

(The binary path is optional; it defaults to `resources/DrawSprocketLib`.)

Prints the `dsp_export_set.json` shape to stdout: `type`, `source_binary`,
`source_sha256` (computed at generation time via `hashlib`), the integer
`exported_symbol_count` (== 53), the **sorted** 53-name `exports` array,
`generated_by`, and `notes`. Deterministic across re-runs.

This output is committed as the test fixture
`SheepShaver/src/MacOSX/PocketShaverTests/DSp/DSpReferenceFixtures/dsp_export_set.json`,
which the install-table drift gate and the `DSpExportSet` Codable consume.

The parser validates the `Joy!`/`peff`/`pwpc` PEF magic and bounds every
table read against the file length, failing **loudly** (exit code 2 on
stderr) on any out-of-range offset rather than silently emitting a short
list (threat T-22.5.1-01).

## Reproduce 2 — build + run the offline PPC disassembler

Build (run from the repo root):

```sh
clang++ -std=c++14 -include tools/dsp_pef/sysdeps_shim.h \
    -I tools/dsp_pef -I cxmon/src \
    -o ppcdis tools/dsp_pef/ppcdis.cpp cxmon/src/mon_ppc.cpp
```

Disassemble the DrawSprocketLib code section starting at file offset 160:

```sh
./ppcdis resources/DrawSprocketLib 160
```

Expected (PPC routine prologue — valid mnemonics, offline):

```
mflr	r0
stmw	r28,$fffffff0(r1)
stw	r0,$0008(r1)
stwu	r1,$ffffffb0(r1)
...
```

A third argument sets the word count (default 16):
`./ppcdis resources/DrawSprocketLib 160 32`.

## Why these tools exist (D-02 rationale)

`FindLibSymbol` (the runtime symbol resolver) delegates to the Mac OS ROM's
CFM and parses **nothing** host-side, so there was no existing host PEF
loader to lift — a new offline parser was required. Apple's bundled
`objdump`/LLVM has **no PowerPC target**, so the in-repo cxmon `disass_ppc`
is the only reproducible offline PPC disassembler available; `kpx_cpu` is
runtime-coupled and unsuitable for standalone file-offset disassembly.
