/*
 *  sysdeps_shim.h - minimal typedef shim so cxmon mon.h / mon_ppc.cpp
 *  compile standalone (offline, host-side) WITHOUT the autoconf-generated
 *  config.h that cxmon's own sysdeps.h normally pulls in.
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
 *  cxmon's `sysdeps.h` expects an
 *  autoconf `config.h`; this shim is force-included (-include) so the only
 *  in-repo decoder we need (cxmon/src/mon_ppc.cpp::disass_ppc) compiles
 *  against the fixed-width types it references, with no build-system setup.
 *  Provides ONLY what cxmon's mon.h / mon_ppc.cpp need: uint32/uint16/uint8,
 *  int32/int16/int8, and uintptr.
 */

#ifndef DSP_PEF_SYSDEPS_SHIM_H
#define DSP_PEF_SYSDEPS_SHIM_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* Define SYSDEPS_H so cxmon's real sysdeps.h (which #include "config.h")
 * is bypassed if it is ever transitively included. */
#ifndef SYSDEPS_H
#define SYSDEPS_H
#endif

typedef uint8_t   uint8;
typedef int8_t    int8;
typedef uint16_t  uint16;
typedef int16_t   int16;
typedef uint32_t  uint32;
typedef int32_t   int32;
typedef uint64_t  uint64;
typedef int64_t   int64;

typedef uintptr_t uintptr;

#endif /* DSP_PEF_SYSDEPS_SHIM_H */
