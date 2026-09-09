/* Keep generated data in its own translation unit, as the hull's own model is:
 * editing the demo's animation should not feed the fighter's vertices through
 * the optimizer again. */
#define OMEGA_FURY_IMPLEMENTATION
#include "omega_fury.h"
