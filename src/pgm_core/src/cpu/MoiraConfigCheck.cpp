// Moira resolves "MoiraConfig.h" beside its own sources before it looks on the
// include path. Updating libextern/Moira by copying upstream's directory whole
// would bring upstream's configuration back and silently replace ours, so the
// build refuses it here. See docs/decisions/0009-moira-configuration.md.

#include "Moira.h"

#ifndef PGM_MOIRA_CONFIG
#error "Moira was compiled with a MoiraConfig.h other than src/pgm_core/moira/MoiraConfig.h"
#endif

static_assert( MOIRA_PRECISE_TIMING, "the bus charges wait states per access" );
static_assert( !MOIRA_MIMIC_MUSASHI, "the machine wants a 68000, not Musashi" );
