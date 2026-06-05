/*
 *  rave_texture_alpha_policy.h - RAVE texture alpha policy helpers.
 *
 *  (C) 2026 Sierra Burkhart (sierra760)
 */

#ifndef RAVE_TEXTURE_ALPHA_POLICY_H
#define RAVE_TEXTURE_ALPHA_POLICY_H

#include <stdint.h>

#define RAVE_QAPIXEL_ALPHA1       0
#define RAVE_QAPIXEL_RGB16        1
#define RAVE_QAPIXEL_ARGB16       2
#define RAVE_QAPIXEL_RGB32        3
#define RAVE_QAPIXEL_ARGB32       4
#define RAVE_QAPIXEL_CL4          5
#define RAVE_QAPIXEL_CL8          6
#define RAVE_QAPIXEL_RGB16_565    7
#define RAVE_QAPIXEL_RGB24        8
#define RAVE_QAPIXEL_RGB8_332     9
#define RAVE_QAPIXEL_ARGB16_4444 10
#define RAVE_QAPIXEL_ACL16_88    11
#define RAVE_QAPIXEL_I8          12
#define RAVE_QAPIXEL_AI16_88     13
#define RAVE_QAPIXEL_YUVS        14
#define RAVE_QAPIXEL_YUVU        15

#define RAVE_GL_ZERO                      0x0000u
#define RAVE_GL_ONE                       0x0001u
#define RAVE_GL_SRC_COLOR                 0x0300u
#define RAVE_GL_ONE_MINUS_SRC_COLOR       0x0301u
#define RAVE_GL_SRC_ALPHA                 0x0302u
#define RAVE_GL_ONE_MINUS_SRC_ALPHA       0x0303u
#define RAVE_GL_DST_ALPHA                 0x0304u
#define RAVE_GL_ONE_MINUS_DST_ALPHA       0x0305u
#define RAVE_GL_DST_COLOR                 0x0306u
#define RAVE_GL_ONE_MINUS_DST_COLOR       0x0307u
#define RAVE_GL_SRC_ALPHA_SATURATE        0x0308u

static inline int RaveTexturePixelTypeCarriesAlpha(int pixel_type)
{
	switch (pixel_type) {
	case RAVE_QAPIXEL_ALPHA1:
	case RAVE_QAPIXEL_ARGB16:
	case RAVE_QAPIXEL_ARGB32:
	case RAVE_QAPIXEL_CL4:
	case RAVE_QAPIXEL_CL8:
	case RAVE_QAPIXEL_ARGB16_4444:
	case RAVE_QAPIXEL_ACL16_88:
	case RAVE_QAPIXEL_AI16_88:
		return 1;
	default:
		return 0;
	}
}

static inline int RaveGLBlendFactorUsesSourceAlpha(uint32_t factor)
{
	return factor == RAVE_GL_SRC_ALPHA ||
	       factor == RAVE_GL_ONE_MINUS_SRC_ALPHA ||
	       factor == RAVE_GL_SRC_ALPHA_SATURATE;
}

static inline int RaveGLBlendFactorsUseSourceAlpha(uint32_t src_factor,
                                                   uint32_t dst_factor)
{
	return RaveGLBlendFactorUsesSourceAlpha(src_factor) ||
	       RaveGLBlendFactorUsesSourceAlpha(dst_factor);
}

static inline int RaveTextureRgbCoverageIsMostlyBlack(uint32_t rgb_nonzero_pixels,
                                                       uint32_t total_pixels)
{
	if (total_pixels == 0) return 0;
	return rgb_nonzero_pixels <= (total_pixels / 16u);
}

static inline int RaveTextureShouldApplyVertexAlphaToOpacityForBlendFactors(
	int pixel_type,
	int blend_mode,
	int alpha_test_func,
	int is_bitmap,
	uint32_t gl_blend_src,
	uint32_t gl_blend_dst)
{
	if (is_bitmap) return 1;
	if (alpha_test_func != 0 && alpha_test_func != 7) return 1;
	if (RaveTexturePixelTypeCarriesAlpha(pixel_type)) return 1;
	if (blend_mode == 1) return 1;
	(void)gl_blend_src;
	(void)gl_blend_dst;
	return 0;
}

static inline int RaveTextureShouldApplyVertexAlphaToOpacityForDraw(
	int pixel_type,
	int blend_mode,
	int alpha_test_func,
	int is_bitmap,
	uint32_t gl_blend_src,
	uint32_t gl_blend_dst,
	uint32_t texture_rgb_nonzero_pixels,
	uint32_t texture_total_pixels)
{
	if (RaveTextureShouldApplyVertexAlphaToOpacityForBlendFactors(
	        pixel_type,
	        blend_mode,
	        alpha_test_func,
	        is_bitmap,
	        gl_blend_src,
	        gl_blend_dst)) {
		return 1;
	}

	if (pixel_type == RAVE_QAPIXEL_RGB16 &&
	    blend_mode == 2 &&
	    RaveGLBlendFactorsUseSourceAlpha(gl_blend_src, gl_blend_dst) &&
	    RaveTextureRgbCoverageIsMostlyBlack(texture_rgb_nonzero_pixels,
	                                        texture_total_pixels)) {
		return 1;
	}

	return 0;
}

static inline int RaveTextureShouldApplyVertexAlphaToOpacity(int pixel_type,
                                                             int blend_mode,
                                                             int alpha_test_func,
                                                             int is_bitmap)
{
	return RaveTextureShouldApplyVertexAlphaToOpacityForBlendFactors(
		pixel_type,
		blend_mode,
		alpha_test_func,
		is_bitmap,
		RAVE_GL_SRC_ALPHA,
		RAVE_GL_ONE_MINUS_SRC_ALPHA);
}

#endif /* RAVE_TEXTURE_ALPHA_POLICY_H */
