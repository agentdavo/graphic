/* Hardware integration fixture. Run under Debug validation; --record captures
 * all resolve state so the independent replay executable can verify it too. */
#include "vkmin.h"
#include "vkmin_cvar.h"
#include "shaders.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(int argc, char **argv) {
    const bool config_test = getenv("VKMIN_TEST_CONFIG") != NULL;
    vkmin_ctx *c = vkmin_init(&(vkmin_desc){.argc=argc,.argv=argv,.headless=true,.width=64,.height=64,
        .vsync=config_test,.sync_naive=config_test,.no_readback=config_test});
    const cvar_state *cfg = vkmin_frame_config(c);
    const uint32_t samples = (uint32_t)cvar_get_int(cfg,CV_r_msaa);
    const uint32_t supported = vkmin_sample_counts(c,VKMIN_FMT_RGBA8_UNORM,VKMIN_IMAGE_COLOR)
        & vkmin_sample_counts(c,VKMIN_FMT_D32_FLOAT,VKMIN_IMAGE_DEPTH);
    const vkmin_msaa_info caps = vkmin_msaa_capabilities(c);
    fprintf(stderr,"test_msaa: samples mask=%u depth resolves=%u render-to-single=%d\n",
            supported,caps.depth_resolve_modes,caps.render_to_single_sampled);
    if (!(supported & samples)) { fprintf(stderr,"SKIP unsupported sample count\n"); vkmin_shutdown(c); return 0; }
    const bool single = cvar_get_bool(cfg,CV_r_msaa_single);
    if (single && (samples < 2 || !caps.render_to_single_sampled)) { fprintf(stderr,"SKIP unsupported extension\n"); vkmin_shutdown(c); return 0; }
    const char *invalid = getenv("VKMIN_TEST_INVALID");
    vkmin_target target = vkmin_make_target(c,&(vkmin_target_desc){.width=64,.height=64,
        .depth=true,.resolve_depth=true,.extra_colors=1,.label="helper"});
    VKMIN_ASSERT(target.samples == samples && target.render_to_single_sampled == single, "target negotiation");
    vkmin_target fallback = vkmin_make_target(c,&(vkmin_target_desc){.width=8,.height=8,
        .depth=true,.color_format=VKMIN_FMT_NONE,.samples=64,.storage=VKMIN_MSAA_PREFER_SINGLE,.label="fallback"});
    VKMIN_ASSERT(fallback.samples <= 64 && (supported & fallback.samples), "target fallback");
    vkmin_free_target(c,&fallback);
    VKMIN_ASSERT(!fallback.pass.depth.id && !fallback.samples, "target free clears bundle");
    const vkmin_buffer tiny=vkmin_make_buffer(c,&(vkmin_buffer_desc){.size=16,.label="bounds fixture"});
    if (invalid && !strcmp(invalid,"upload")) {
        const uint64_t value=0;
        vkmin_buffer_upload(c,tiny,SIZE_MAX-3,VKMIN_BYTES(value));
        VKMIN_FAIL("overflow upload was accepted");
    }
    vkmin_target vertex_target=vkmin_make_target(c,&(vkmin_target_desc){.width=64,.height=64,
        .samples=1,.storage=VKMIN_MSAA_EXPLICIT,.extra_colors=1,.label="vertex sample"});
    const uint32_t texture=vkmin_index(c,target.color);
    const vkmin_pipeline vertex_pipe=vkmin_make_pipeline(c,&(vkmin_pipeline_desc){
        .vs=VKMIN_BYTES(ex_vertex_sample_vert_spv),.fs=VKMIN_BYTES(ex_msaa_frag_spv),.push_size=sizeof(uint32_t),
        .extra_colors=1,.cull=VKMIN_CULL_NONE,.label="vertex texture read"});
    const bool coverage = cvar_get_bool(cfg,CV_r_alpha_to_coverage);
    const vkmin_image_desc color_desc = {.width=64,.height=64,.usage=VKMIN_IMAGE_COLOR,
        .samples=single?1:samples,.render_to_single_sampled=single,.label="color samples"};
    const vkmin_image color=vkmin_make_image(c,&color_desc);
    vkmin_image_desc extra_desc=color_desc; extra_desc.label="extra samples";
    const vkmin_image extra=vkmin_make_image(c,&extra_desc);
    const vkmin_image depth=vkmin_make_image(c,&(vkmin_image_desc){.width=64,.height=64,
        .format=VKMIN_FMT_D32_FLOAT,.usage=VKMIN_IMAGE_DEPTH,.samples=single?1:samples,.render_to_single_sampled=single,.label="depth samples"});
    const vkmin_image extra_resolve=vkmin_make_image(c,&(vkmin_image_desc){.width=64,.height=64,
        .usage=VKMIN_IMAGE_COLOR,.label="extra resolve"});
    const vkmin_image depth_resolve=vkmin_make_image(c,&(vkmin_image_desc){.width=64,.height=64,
        .format=VKMIN_FMT_D32_FLOAT,.usage=VKMIN_IMAGE_DEPTH,.label="depth resolve"});
    const vkmin_pipeline pipe=vkmin_make_pipeline(c,&(vkmin_pipeline_desc){
        .vs=VKMIN_BYTES(ex_tri_vert_spv),.fs=VKMIN_BYTES(ex_msaa_frag_spv),.push_size=sizeof(float),
        .samples=invalid && !strcmp(invalid,"pipeline")?(samples==1?2u:1u):samples,.alpha_to_coverage=coverage,.depth=true,.depth_write=true,.cull=VKMIN_CULL_NONE,
        .extra_colors=1,.label="MSAA MRT coverage"});
    const vkmin_pipeline no_test=vkmin_make_pipeline(c,&(vkmin_pipeline_desc){
        .vs=VKMIN_BYTES(ex_tri_vert_spv),.fs=VKMIN_BYTES(ex_msaa_frag_spv),.push_size=sizeof(float),
        .samples=samples,.alpha_to_coverage=coverage,.depth_attachment=true,.cull=VKMIN_CULL_NONE,
        .extra_colors=1,.label="attached depth without testing"});
    while(vkmin_running(c)) {
        (void)vkmin_frame_begin(c,NULL);
        const vkmin_stats timing=vkmin_stats_get(c);
        if (timing.timestamps) VKMIN_ASSERT(timing.timestamps==8 && isnan(timing.gpu_ms[1]) &&
            timing.gpu_ms[0]==0 && isfinite(timing.gpu_ms[7]), "sparse timestamps not preserved");
        vkmin_timestamp(c,0);
        if (invalid && !strcmp(invalid,"ring")) {
            (void)vkmin_ring_alloc(c,64,NULL);
            (void)vkmin_ring_alloc(c,SIZE_MAX,NULL);
            VKMIN_FAIL("overflow ring allocation was accepted");
        }
        if (invalid && !strcmp(invalid,"fill")) vkmin_fill_buffer(c,tiny,SIZE_MAX-3,8,0);
        if (invalid && !strcmp(invalid,"barrier")) vkmin_barrier(c,&(vkmin_barrier_desc){.image_count=-1});
        vkmin_pass_desc helper_pass = target.pass;
        for (uint32_t mode=1; mode<=8; mode<<=1) if (caps.depth_resolve_modes & mode)
            helper_pass.depth_resolve_mode = (vkmin_resolve)mode;
        if (invalid && !strcmp(invalid,"resolve")) helper_pass.color_resolve = target.pass.color;
        if (invalid && !strcmp(invalid,"depth")) helper_pass.depth_resolve_mode = (vkmin_resolve)16;
        if (invalid && !strcmp(invalid,"attachment")) helper_pass.depth = depth_resolve;
        vkmin_pass_begin(c,&helper_pass);
        if (invalid && !strcmp(invalid,"push"))
            vkmin_draw_indirect(c,pipe,NULL,&(vkmin_indirect_desc){.indices=tiny});
        const float helper_alpha = 0.0f;
        if (invalid && !strcmp(invalid,"indirect"))
            vkmin_draw_indirect(c,pipe,&helper_alpha,&(vkmin_indirect_desc){.indices=tiny,.cmds=tiny,.max_draws=1});
        vkmin_draw(c,no_test,&helper_alpha,3,1);
        vkmin_draw(c,pipe,&helper_alpha,3,1); vkmin_pass_end(c);
        const vkmin_transition ready={target.color,VKMIN_USE_SAMPLED};
        vkmin_barrier(c,&(vkmin_barrier_desc){.images=&ready,.image_count=1});
        vkmin_pass_begin(c,&vertex_target.pass);
        vkmin_draw(c,vertex_pipe,&texture,3,1); vkmin_pass_end(c);
        /* Every supported depth mode is exercised in order, with LOAD hazards
         * between passes and a new clear on the next frame. */
        for(uint32_t mode=1;mode<=8;mode<<=1) if(caps.depth_resolve_modes&mode) {
            vkmin_pass_begin(c,&(vkmin_pass_desc){.color=samples>1?color:vkmin_backbuffer(c),.extra={samples>1?extra:extra_resolve},.depth=depth,
                .color_resolve=single||samples==1?(vkmin_image){0}:vkmin_backbuffer(c),
                .extra_resolve={single||samples==1?(vkmin_image){0}:extra_resolve},
                .depth_resolve=single||samples==1?(vkmin_image){0}:depth_resolve,.depth_resolve_mode=(vkmin_resolve)mode,
                .raster_samples=single?samples:0,.clear_color=true,.clear_depth=true,.label="MSAA resolves"});
            const float alpha=0.0f;
            vkmin_draw(c,pipe,&alpha,3,1); vkmin_pass_end(c);
        }
        /* Extension outputs are the single-sample color/depth themselves; the
         * ordinary path resolves into the backbuffer and named outputs. */
        if(single) {
            vkmin_pass_begin(c,&(vkmin_pass_desc){.color=vkmin_backbuffer(c),.depth=depth_resolve,
                .clear_color=true,.clear_depth=true,.label="present clear"});
            vkmin_pass_end(c);
        }
        vkmin_timestamp(c,7);
        vkmin_frame_end(c);
    }
    vkmin_free_target(c,&vertex_target);
    vkmin_free_buffer(c,tiny);
    vkmin_free_target(c,&target);
    vkmin_shutdown(c);
    return 0;
}
