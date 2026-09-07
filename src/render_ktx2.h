/* render_ktx2.h -- loads a BCn KTX2 file written by the cooker (tools/cook.c,
 * not currently in the tree) into a vkmin image with its whole mip chain.
 * Deliberately not a general KTX2 reader: only what the cooker writes is
 * accepted -- uncompressed (no supercompression), 2D, one layer, one face,
 * BC1/BC3/BC4/BC5 -- so the parser stays small enough to audit. Anything else
 * aborts with a message rather than guessing. Returns the image; the caller
 * registers it to get a bindless slot. */
#ifndef VKMIN_KTX2_H
#define VKMIN_KTX2_H

#include "vkmin.h"

vkmin_image ktx2_load(vkmin_ctx *c, const char *path);

#endif
