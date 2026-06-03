#ifndef DSP_MAIN_DEVICE_REDIRECT_POLICY_H
#define DSP_MAIN_DEVICE_REDIRECT_POLICY_H

#include <stdint.h>

#include "dsp_display_mode_policy.h"

static inline uint32_t DSpMainDevicePixMapDepth(uint32_t back_buffer_depth,
                                                uint32_t display_depth)
{
	return DSpDisplayModeDepth(back_buffer_depth, display_depth);
}

static inline uint32_t DSpMainDevicePixMapRowBytes(uint32_t width,
                                                   uint32_t back_buffer_depth,
                                                   uint32_t display_depth)
{
	return DSpDisplayModeRowBytes(
		width,
		DSpMainDevicePixMapDepth(back_buffer_depth, display_depth));
}

static inline uint16_t DSpMainDevicePixMapRowBytesField(uint32_t row_bytes)
{
	return (uint16_t)(0x8000u | (row_bytes & 0x3FFFu));
}

static inline bool DSpShouldRedirectMainDevicePixMap(uint32_t back_buffer_depth,
                                                     uint32_t display_depth)
{
	return back_buffer_depth != 0 &&
	       display_depth != 0 &&
	       DSpMainDevicePixMapDepth(back_buffer_depth, display_depth) != 0;
}

static inline bool DSpShouldCacheMainDevicePixMapOriginal(bool saved_valid,
                                                          uint32_t saved_pixmap_addr,
                                                          uint32_t current_pixmap_addr)
{
	return !saved_valid || saved_pixmap_addr != current_pixmap_addr;
}

#endif /* DSP_MAIN_DEVICE_REDIRECT_POLICY_H */
