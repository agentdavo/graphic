/* amalg_check.c -- proves the generated single header compiles and links on
 * its own: one translation unit, the implementation define, nothing else. */
#define VKMIN_IMPLEMENTATION
#include "vkmin_single.h"

int main(void) {
    const vkmin_report r = vkmin_probe(0);
    return r.vulkan_1_3 ? 0 : 1;
}
