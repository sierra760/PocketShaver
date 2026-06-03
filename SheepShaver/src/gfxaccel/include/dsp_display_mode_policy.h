/*
 *  dsp_display_mode_policy.h - DrawSprocket display mode selection helpers.
 */

#ifndef DSP_DISPLAY_MODE_POLICY_H
#define DSP_DISPLAY_MODE_POLICY_H

#include <stdint.h>

static inline uint32_t DSpDisplayModeDepth(uint32_t back_buffer_depth,
                                           uint32_t display_depth)
{
	switch (display_depth) {
	case 1:
	case 2:
	case 4:
	case 8:
	case 16:
	case 32:
		return display_depth;
	default:
		break;
	}
	return back_buffer_depth;
}

static inline uint32_t DSpDisplayModeRowBytes(uint32_t width,
                                              uint32_t display_depth)
{
	return (width * display_depth + 7u) / 8u;
}

static inline uint32_t DSpDisplayModePitch(uint32_t width,
                                           uint32_t display_depth)
{
	const uint32_t row_bytes = DSpDisplayModeRowBytes(width, display_depth);
	return (row_bytes + 255u) & ~255u;
}

#endif /* DSP_DISPLAY_MODE_POLICY_H */
