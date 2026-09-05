/* Compile the bundled parser separately from our strict-warning cooker,
 * like cook_image.c and stb_bridge.c. No project warning is disabled. */
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
