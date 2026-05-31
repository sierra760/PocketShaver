#ifndef GL_METAL_DRAW_STATE_H
#define GL_METAL_DRAW_STATE_H

/*
 * Fixed-function draw state helpers shared by the renderer and focused tests.
 *
 * glActiveTextureARB selects the texture unit modified by subsequent texture
 * state calls. It does not move legacy GL_TEXTURE_2D sampling for texcoord0
 * away from unit 0. Unit 1 remains a separate multitexture input.
 */
static inline int GLMetalPrimaryTextureUnitForDraw(int active_texture)
{
	(void)active_texture;
	return 0;
}

/*
 * GL overlays are composited over the Mac framebuffer as an opaque 3D layer.
 * Preserve the render-pass alpha clear value (1.0) and apply only the guest RGB
 * write mask; otherwise unlit GL draws with alpha 0 punch holes into the overlay
 * and are later blended as if they were premultiplied.
 */
static inline unsigned GLMetalOverlayColorWriteMask(bool red, bool green,
                                                    bool blue, bool alpha)
{
	(void)alpha;
	return (red ? 1u : 0u) | (green ? 2u : 0u) | (blue ? 4u : 0u);
}

#endif /* GL_METAL_DRAW_STATE_H */
