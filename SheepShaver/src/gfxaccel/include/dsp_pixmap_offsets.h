/*
 *  dsp_pixmap_offsets.h - canonical PixMap field offsets + Mac OS lowmem globals
 *
 *  (C) 2026 Sierra Burkhart (sierra760)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Field offsets and Mac OS lowmem globals used by the
 *  DSpRedirectMainDevicePixMap / DSpRestoreMainDevicePixMap helpers. The
 *  DSP_PIXMAP_OFF_* values match the Mac OS 9.0.4 PixMap layout exercised by
 *  the GetBackBuffer CGrafPtr write path, and the canonical layout in Inside
 *  Macintosh: Imaging With QuickDraw ch.4.
 */
#ifndef DSP_PIXMAP_OFFSETS_H
#define DSP_PIXMAP_OFFSETS_H

/* PixMap struct field offsets (bytes from PixMap struct base). */
#define DSP_PIXMAP_OFF_BASEADDR     0    /* 4 bytes  — baseAddr      */
#define DSP_PIXMAP_OFF_ROWBYTES     4    /* 2 bytes  — rowBytes      */
#define DSP_PIXMAP_OFF_BOUNDS_TOP   6    /* 2 bytes  — bounds.top    */
#define DSP_PIXMAP_OFF_BOUNDS_LEFT  8    /* 2 bytes  — bounds.left   */
#define DSP_PIXMAP_OFF_BOUNDS_BOT  10    /* 2 bytes  — bounds.bottom */
#define DSP_PIXMAP_OFF_BOUNDS_RIGHT 12   /* 2 bytes  — bounds.right  */
#define DSP_PIXMAP_OFF_PIXELTYPE   14    /* 2 bytes  — pixelType     */
#define DSP_PIXMAP_OFF_PIXELSIZE   16    /* 2 bytes  — pixelSize     */
#define DSP_PIXMAP_OFF_CMPCOUNT    18    /* 2 bytes  — cmpCount      */
#define DSP_PIXMAP_OFF_CMPSIZE     20    /* 2 bytes  — cmpSize       */

/* Mac OS lowmem globals (Inside Macintosh: Imaging With QuickDraw ch.4).
 * LMADDR_MAIN_DEVICE holds a GDeviceHandle; double-dereference + add
 * GDEVICE_OFF_PMAP yields the PixMapHandle.
 *
 * GDevice struct layout (M68K, big-endian, classic Mac OS):
 *   0x00  gdRefNum      int16   driver refnum
 *   0x02  gdID          int16
 *   0x04  gdType        int16
 *   0x06  gdITable      Handle  inverse table (4 bytes)
 *   0x0A  gdResPref     int16
 *   0x0C  gdSearchProc  Ptr     (4 bytes)
 *   0x10  gdCompProc    Ptr     (4 bytes)
 *   0x14  gdFlags       int16   <— NOTE: 2 bytes, not 4
 *   0x16  gdPMap        Handle  PixMapHandle <— this is what we want
 *   ...
 *
 * Canonical cross-reference: BasiliskII/src/video.cpp:439 uses
 *   `ReadMacInt32(gdev + 0x16)` with the comment `// gdPMap`.
 *
 * Note: 0x14 is gdFlags (2 bytes), not the PixMapHandle; reading the
 * PixMapHandle from 0x14 returns garbage (e.g. 0xbc211000) that the OOB
 * gate must refuse. The correct offset is 0x16. */
#define LMADDR_MAIN_DEVICE  0x8A4   /* GDeviceHandle */
#define GDEVICE_OFF_PMAP    0x16    /* GDevice.gdPMap (PixMapHandle) */

#endif /* include guard */
