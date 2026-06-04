#ifndef GL_OFFSCREEN_POLICY_H
#define GL_OFFSCREEN_POLICY_H

#include <stdint.h>

static inline bool GLShouldAcceptOffscreenDrawable(uint32_t width,
                                                   uint32_t height,
                                                   uint32_t rowbytes,
                                                   uint32_t baseaddr)
{
	const uint32_t min_rowbytes = width * 2;

	return width != 0 &&
	       height != 0 &&
	       min_rowbytes >= width &&
	       rowbytes >= min_rowbytes &&
	       baseaddr != 0;
}

#endif /* GL_OFFSCREEN_POLICY_H */
