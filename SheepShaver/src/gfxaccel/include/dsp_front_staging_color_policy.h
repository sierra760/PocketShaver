/*
 *  dsp_front_staging_color_policy.h - color-transform policy for DSp presents.
 */

#ifndef DSP_FRONT_STAGING_COLOR_POLICY_H
#define DSP_FRONT_STAGING_COLOR_POLICY_H

#include <stdbool.h>
#include <stdint.h>

static inline bool DSpBackBufferPresentUsesDisplayGamma(void)
{
	return true;
}

static inline bool DSpFrontStagingUsesDisplayGamma(void)
{
	return false;
}

static inline bool DSpFrontStagingUsesNativeR16OrderInMetal(void)
{
	return false;
}

static inline bool DSpFrontStagingWritesCompositor32Layout(void)
{
	return true;
}

static inline uint32_t DSpUnpackShaderFadeActiveForGammaPolicy(
	bool use_display_gamma,
	bool display_fade_active)
{
	if (!use_display_gamma) return 1u;
	return display_fade_active ? 1u : 0u;
}

#endif /* DSP_FRONT_STAGING_COLOR_POLICY_H */
