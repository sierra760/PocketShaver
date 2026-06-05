/*
 *  metal_compositor_drawable_policy.h - Metal compositor drawable size helpers.
 *
 *  (C) 2026 Sierra Burkhart (sierra760)
 */

#ifndef METAL_COMPOSITOR_DRAWABLE_POLICY_H
#define METAL_COMPOSITOR_DRAWABLE_POLICY_H

struct MetalCompositorDrawableSize {
	int width;
	int height;
};

static inline MetalCompositorDrawableSize MetalCompositorTargetDrawableSize(
	int framebuffer_width,
	int framebuffer_height,
	int view_width,
	int view_height)
{
	MetalCompositorDrawableSize size;
	if (view_width > 0 && view_height > 0) {
		size.width = view_width;
		size.height = view_height;
	} else {
		size.width = framebuffer_width;
		size.height = framebuffer_height;
	}
	return size;
}

#endif /* METAL_COMPOSITOR_DRAWABLE_POLICY_H */
