/* Typed capture: pointer fields and identical integers are distinct. */
#include "vkmin.h"
#include "shaders.h"
#include <string.h>
#include <stddef.h>

typedef struct { uint64_t address, integer; } Push;
int main(int argc, char **argv) {
    vkmin_ctx *c = vkmin_init(&(vkmin_desc){.argc=argc,.argv=argv,.headless=true,.width=32,.height=32});
    const float values[] = {0.0f, 0.75f, 0.25f, 1.0f};
    const vkmin_buffer source = vkmin_make_buffer(c,&(vkmin_buffer_desc){.data=VKMIN_BYTES(values),.label="address source"});
    const uint64_t address = vkmin_address(c,source)+4;
    const uint32_t field[] = {1}, push_field[] = {offsetof(Push,address)};
    unsigned char packed[24] = {0};
    memcpy(packed+1,&address,8); memcpy(packed+9,&address,8);
    const vkmin_buffer packed_buffer = vkmin_make_buffer(c,&(vkmin_buffer_desc){.data=VKMIN_BYTES(packed),
        .addresses={field,1},.label="packed pointer and integer"});
    vkmin_buffer_upload_typed(c,packed_buffer,0,VKMIN_BYTES(packed),(vkmin_address_layout){field,1});
    const vkmin_pipeline p = vkmin_make_pipeline(c,&(vkmin_pipeline_desc){
        .vs=VKMIN_BYTES(ex_tri_vert_spv),.fs=VKMIN_BYTES(ex_relocation_frag_spv),.push_size=sizeof(Push),
        .push_addresses={push_field,1},.depth_attachment=true,.cull=VKMIN_CULL_NONE,.label="interior address"});
    while(vkmin_running(c)) {
        (void)vkmin_frame_begin(c,&(vkmin_clear){0,0,0,1});
        uint64_t ring_address=0;
        unsigned char *ring=vkmin_ring_alloc_typed(c,sizeof packed,&ring_address,(vkmin_address_layout){field,1});
        memcpy(ring,packed,sizeof packed);
        const Push push={address,address};
        vkmin_draw(c,p,&push,3,1);
        vkmin_frame_end(c);
    }
    vkmin_free_buffer(c,packed_buffer); vkmin_free_buffer(c,source);
    vkmin_shutdown(c);
    return 0;
}
