/*
 *  rave_texture_snapshot_policy.h - RAVE texture snapshot ownership helpers.
 *
 *  (C) 2026 Sierra Burkhart (sierra760)
 */

#ifndef RAVE_TEXTURE_SNAPSHOT_POLICY_H
#define RAVE_TEXTURE_SNAPSHOT_POLICY_H

#include <stdint.h>

/*
 * Direct-format QATextureNew normally has copy semantics. Some clients pass
 * an initially empty buffer and fill it shortly after, so those must remain
 * deferred. If converted level-0 pixels already contain any observable BGRA
 * data, snapshot immediately so later scratch-buffer reuse cannot corrupt the
 * texture identity.
 */
static inline int RaveDirectTextureShouldSnapshotConvertedSource(uint32_t converted_nonzero)
{
	return converted_nonzero != 0;
}

#endif /* RAVE_TEXTURE_SNAPSHOT_POLICY_H */
