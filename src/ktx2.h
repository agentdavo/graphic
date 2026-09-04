/* ktx2.h -- loads a BCn KTX2 file written by tools/cook.c into a vkmin image
 * with its whole mip chain, and returns the bindless slot. Only what the
 * cooker writes is accepted: uncompressed (no supercompression), 2D, one
 * layer, one face, BC1/BC3/BC4/BC5. Anything else aborts with a message. */
#ifndef VKMIN_KTX2_H
#define VKMIN_KTX2_H

#include "vkmin.h"

vkmin_image ktx2_load(vkmin_ctx *c, const char *path);

#endif
