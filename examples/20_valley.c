/* 20_valley.c -- v0.5 outside. Demo-only: this camera route, morning schedule
 * and asset selection. Terrain, scatter, wind, sky, foliage, water and post
 * are public primitives (render.h, vkmin.h, shaders/lib/outdoor.glsl).
 * Nothing reads a clock. Asset authoring is offline: tools/bake_valley.py.
 */
#include "gamekit.h"
#include "stb_bridge.h"
#include "valley_path.h"
#ifdef SNDMIN_VALLEY
#include "valley_audio.h"
#endif

enum { MAX_INSTANCES = 16384, KEY_CAP = 16 };

static uint32_t load_texture(vkmin_ctx *gpu, const char *name, bool clamp) {
    char path[256];
    snprintf(path,sizeof path,"assets/valley/%s.png",name);
    int w, h;
    unsigned char *pixels = vkmin_png_load(path,&w,&h);
    if (!pixels) gk_die("missing valley texture");
    const bool srgb = !strcmp(name,"leaf") || !strcmp(name,"flower") || !strcmp(name,"tree_atlas") || !strcmp(name,"bark");
    const bool masked = !strcmp(name,"leaf") || !strcmp(name,"flower") || !strcmp(name,"tree_atlas");
    int levels = 1;
    for (int size = w > h ? w : h; size > 1; size /= 2) ++levels;
    const vkmin_image image = vkmin_make_image(gpu,&(vkmin_image_desc){.width = w, .height = h, .mip_levels = levels,
        .format = srgb ? VKMIN_FMT_RGBA8_SRGB : VKMIN_FMT_RGBA8_UNORM,
        .sampler = clamp ? VKMIN_SAMPLER_LINEAR_CLAMP : VKMIN_SAMPLER_ANISO_REPEAT, .label = name});
    for (int mip = 0; mip < levels; ++mip) {
        vkmin_image_upload(gpu,image,mip,(vkmin_bytes){pixels,(size_t)w*(size_t)h*4});
        if (mip+1 == levels) break;
        const int nw = w > 1 ? w/2 : 1, nh = h > 1 ? h/2 : 1;
        size_t covered = 0;
        for (int k = 0; k < w*h; ++k) covered += pixels[k*4+3] >= 128;
        const float coverage = (float)covered/(float)(w*h);
        for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) {
            unsigned int sum[4] = {0};
            for (int j = 0; j < 4; ++j) {
                const int sx = x*2+(w > 1 ? j%2 : 0), sy = y*2+(h > 1 ? j/2 : 0);
                const unsigned char *pixel = pixels+(sy*w+sx)*4;
                for (int c = 0; c < 3; ++c) sum[c] += pixel[c]*(masked ? pixel[3] : 1u);
                sum[3] += pixel[3];
            }
            unsigned char *out = pixels+(y*nw+x)*4;
            for (int c = 0; c < 3; ++c) out[c] = (unsigned char)(sum[c]/(masked ? (sum[3] ? sum[3] : 1u) : 4u));
            out[3] = (unsigned char)(sum[3]/4u);
        }
        if (masked) {
            float lo = 0, hi = 8;
            for (int search = 0; search < 10; ++search) {
                const float scale = (lo+hi)*.5f; size_t count = 0;
                for (int k = 0; k < nw*nh; ++k) count += pixels[k*4+3]*scale >= 128;
                if ((float)count/(float)(nw*nh) < coverage) lo = scale; else hi = scale;
            }
            for (int k = 0; k < nw*nh; ++k) pixels[k*4+3] = (unsigned char)fminf(255,pixels[k*4+3]*hi);
        }
        w = nw; h = nh;
    }
    vkmin_png_free(pixels);
    return vkmin_index(gpu,image);
}

static uint32_t read_tree(FILE *file, vkr *r) {
    uint32_t vn = 0, in = 0;
    if (fscanf(file,"%u %u",&vn,&in) != 2 || vn == 0 || vn > 4096 || in == 0 || in > 16384) gk_die("bad tree mesh counts");
    gk_mesh_builder b = {0};
    gk_mb_reserve(&b,vn,in);
    float radius = 0;
    for (uint32_t k = 0; k < vn; ++k) {
        vec3 p, n, t; float u, v;
        if (fscanf(file,"%f %f %f %f %f %f %f %f %f %f %f",&p.x,&p.y,&p.z,&n.x,&n.y,&n.z,&t.x,&t.y,&t.z,&u,&v) != 11)
            gk_die("truncated tree vertices");
        if (!isfinite(p.x+p.y+p.z+n.x+n.y+n.z+t.x+t.y+t.z+u+v)) gk_die("invalid tree vertex");
        radius = fmaxf(radius,vkmin_vec3_length(p));
        gk_mb_vertex(&b,p,n,t,u,v);
    }
    for (uint32_t k = 0; k < in; ++k) if (fscanf(file,"%u",&b.i[k]) != 1 || b.i[k] >= vn) gk_die("bad tree index");
    b.in = in;
    return gk_upload_mesh(r,&b,radius);
}

int main(int argc, char **argv) {
    cvar_set(CV_taa,1); cvar_set(CV_bloom,0.12f);
    cvar_set(CV_r_shadow_distance,90); cvar_set(CV_r_exposure,1.1f);
    const gk_options options = gk_parse(argc,argv,
        "20_valley --profile=lavapipe --frame N --out valley.png\n"
        "Normal profile: 120s at 60fps. --frame N warms TAA from zero; +taa 0 is isolated.\n"
        "--record FILE captures GPU calls; --replay FILE reproduces them.\n");
    const bool small = options.profile != NULL;
#ifdef SNDMIN_VALLEY
    FILE *shared_journal = NULL;
    const char *audio_out = "tests/out/sndmin/valley.wav";
    const char *audio_png = "tests/out/sndmin/valley.png";
    uint32_t audio_frames = 600;
    for (int k=1;k+1<argc;++k) {
        if (!strcmp(argv[k],"--shared-journal")) shared_journal=jrnl_open(argv[k+1],true);
        if (!strcmp(argv[k],"--audio-out")) audio_out=argv[k+1];
        if (!strcmp(argv[k],"--audio-png")) audio_png=argv[k+1];
        if (!strcmp(argv[k],"--audio-frames")) audio_frames=(uint32_t)strtoul(argv[k+1],NULL,10);
    }
#endif
    if (small) { cvar_set(CV_taa,0); cvar_set(CV_bloom,0); }
    vkmin_ctx *gpu = vkmin_init(&(vkmin_desc){.argc = argc, .argv = argv, .title = "valley",
#ifdef SNDMIN_VALLEY
        .journal = shared_journal,
#endif
        .headless = options.headless, .history = cvar_get_bool(CV_taa), .device_arena_bytes = 512u*1024u*1024u});
    if (small) { cvar_set(CV_taa,0); cvar_set(CV_bloom,0); }
    for (int k = 1; k+1 < argc; ++k) if (!strcmp(argv[k],"--replay")) {
#ifdef SNDMIN_VALLEY
        sndmin_ctx *audio=sndmin_init(&(sndmin_desc){.offline=true});
        if(!audio||!sndmin_replay(audio,argv[k+1])||!sndmin_render(audio,audio_frames,audio_out,audio_png)) gk_die("audio replay failed");
        sndmin_shutdown(audio);
#endif
        const bool ok = vkmin_replay(gpu,argv[k+1]); vkmin_shutdown(gpu); return ok ? 0 : 1;
    }
    int width, height;
    vkmin_size(gpu,&width,&height);
    FILE *file = fopen("assets/valley/scene.txt","r");
    if (!file) gk_die("missing assets/valley/scene.txt (run from repository root)");
    int samples, chunk; float cell, minimum, range, river;
    if (fscanf(file,"%d %d %f %f %f %f",&samples,&chunk,&cell,&minimum,&range,&river) != 6 ||
        samples < 2 || samples > 1025 || chunk <= 0 || samples != chunk*8+1 || cell <= 0 || range <= 0) gk_die("bad valley dimensions");
    uint32_t key_count = 0;
    if (fscanf(file,"%u",&key_count) != 1 || key_count < 2 || key_count > KEY_CAP) gk_die("bad camera key count");
    valley_key keys[KEY_CAP] = {0};
    for (uint32_t k = 0; k < key_count; ++k) {
        valley_key *key = &keys[k];
        if (fscanf(file,"%f %f %f %f %f %f",&key->eye.x,&key->eye.y,&key->eye.z,
                   &key->target.x,&key->target.y,&key->target.z) != 6) gk_die("truncated camera path");
    }
    int w, h;
    unsigned char *image = vkmin_png_load("assets/valley/height.png",&w,&h);
    if (!image || w != samples || h != samples) gk_die("height map dimensions differ from scene.txt");
    const size_t texels = (size_t)samples*(size_t)samples;
    float *heights = malloc(texels*sizeof(float));
    vec4 *map = malloc(texels*sizeof(vec4));
    uint32_t *pixels = malloc(texels*sizeof(uint32_t));
    if (!heights || !map || !pixels) gk_die("out of memory");
    for (size_t k = 0; k < texels; ++k) heights[k] = minimum+(float)(image[k*4]*256u+image[k*4+1])/65535.0f*range;
    vkmin_png_free(image);
    const vkmin_terrain_desc terrain = {.heightfield = {.heights = heights, .width = samples, .height = samples,
        .cell = cell, .chunk = chunk}, .skirt = 18};
    const vkmin_heightfield_size counts = vkmin_terrain_sizes(&terrain);
    vkr *r = vkr_init(gpu,&(vkr_desc){.width = width, .height = height,
        .shadow_atlas = cvar_get_int(CV_r_shadow_atlas), .max_vertices = counts.vertices+8192,
        .max_indices = counts.indices+32768, .max_meshes = 96, .max_materials = 8,
        .max_instances = MAX_INSTANCES, .outdoor = true});
    Vertex *vertices = malloc((size_t)counts.vertices*sizeof(Vertex));
    uint32_t *indices = malloc((size_t)counts.indices*sizeof(uint32_t));
    Mesh nodes[85];
    if (!vertices || !indices) gk_die("out of memory");
    vkmin_terrain(&terrain,vertices,indices,nodes);
    const uint32_t terrain_mesh = vkr_upload_geometry(r,&(vkr_geometry){.vertices = vertices, .vertex_count = counts.vertices,
        .indices = indices, .index_count = counts.indices, .meshes = nodes, .mesh_count = counts.meshes});
    free(vertices); free(indices);
    Outdoor outside = {.terrain = {0,0,(float)(samples-1)*cell,(float)(samples-1)*cell},
        .height = {minimum,range,river,0.00045f}, .weather = {0.48f,0.85f,0.24f,32}, .water = {0.7f,0.7f,0.14f,0.32f}};
    outside.maps.x = load_texture(gpu,"height",true);
    outside.maps.y = load_texture(gpu,"splat",true);
    outside.maps.w = load_texture(gpu,"cloud",false);
    vkmin_terrain_flow(&terrain.heightfield,map,texels);
    for (size_t k = 0; k < texels; ++k) pixels[k] = gk_rgba(map[k].x*.5f+.5f,map[k].z*.5f+.5f,0,1);
    outside.maps.z = gk_texture(gpu,samples,pixels,VKMIN_SAMPLER_LINEAR_CLAMP,"terrain.flow");
    vkmin_terrain_normal(&terrain.heightfield,map,texels);
    free(heights); free(map); free(pixels);
    uint32_t albedo[4], normals[4];
    for (int k = 0; k < 4; ++k) {
        char name[32]; snprintf(name,sizeof name,"albedo%d",k); albedo[k] = load_texture(gpu,name,false);
        snprintf(name,sizeof name,"normal%d",k); normals[k] = load_texture(gpu,name,false);
    }
    outside.albedo = (uvec4){albedo[0],albedo[1],albedo[2],albedo[3]};
    outside.normals = (uvec4){normals[0],normals[1],normals[2],normals[3]};
    outside.water_maps = (uvec4){load_texture(gpu,"water0",false),load_texture(gpu,"water1",false),0,0};
    const uint32_t grass_density = load_texture(gpu,"grass",true), flowers_density = load_texture(gpu,"flowers",true);
    const uint32_t rocks_density = load_texture(gpu,"rocks",true), tree_density = load_texture(gpu,"trees",true);
    const uint32_t tree_atlas = load_texture(gpu,"tree_atlas",true);
    const uint32_t trunk = read_tree(file,r), leaves = read_tree(file,r), shrub = read_tree(file,r);
    fclose(file);
    Material materials[6] = {gk_material(1,1,1,0,0.9f,VKMIN_MAT_TERRAIN),
        gk_material(.18f,.29f,.055f,0,0.9f,VKMIN_MAT_MASKED|VKMIN_MAT_DOUBLE_SIDED),
        gk_material(1,1,1,0,.9f,VKMIN_MAT_MASKED|VKMIN_MAT_DOUBLE_SIDED),
        gk_material(.25f,.27f,.23f,0,.85f,0),gk_material(1,1,1,0,.95f,VKMIN_MAT_DOUBLE_SIDED),
        gk_material(1,1,1,0,.9f,VKMIN_MAT_MASKED|VKMIN_MAT_DOUBLE_SIDED)};
    materials[4].albedo_tex = load_texture(gpu,"bark",false);
    materials[2].albedo_tex = load_texture(gpu,"flower",true);
    materials[5].albedo_tex = load_texture(gpu,"leaf",true);
    const uint32_t mat = vkr_upload_materials(r,materials,6);
    gk_mesh_builder builder = {0};
    const bool patches = cvar_get_bool(CV_r_grass_patch);
    const uint32_t blade_count = patches ? 48u : 1u;
    gk_mb_reserve(&builder,7*blade_count,15*blade_count);
    for (uint32_t blade = 0; blade < blade_count; ++blade) {
    const float angle = (float)blade*2.399963f, cs = cosf(angle), sn = sinf(angle);
    const float radius = patches ? .62f*sqrtf(((float)blade+.5f)/48) : 0;
    const float height_scale = .36f+.25f*(.5f+.5f*sinf((float)blade*7.3f));
    for (int k = 0; k < 7; ++k) {
        const int level = k/2;
        const float y = (float)level/3, half = 0.046f*(1-y), x = k == 6 ? .15f : (k%2 ? half : -half);
        gk_mb_vertex(&builder,(vec3){cs*(radius+x),y*height_scale,sn*(radius+x)},
            (vec3){-sn,.45f,cs},(vec3){cs,0,sn},k%2 ? 1 : 0,y);
    }
    const uint32_t blade_indices[15] = {0,1,2,1,3,2,2,3,4,3,5,4,4,5,6};
    for (uint32_t k = 0; k < 15; ++k) builder.i[builder.in++] = blade*7+blade_indices[k];
    }
    const uint32_t grass = gk_upload_mesh(r,&builder,1.1f);
    gk_build_quad(&builder);
    for (uint32_t k = 0; k < builder.vn; ++k) { builder.v[k].py = (builder.v[k].py+1)*0.5f; builder.v[k].px *= 0.35f; }
    const uint32_t flowers = gk_upload_mesh(r,&builder,1.1f);
    gk_build_sphere(&builder,5,7);
    for (uint32_t k = 0; k < builder.vn; ++k) builder.v[k].py *= 0.6f;
    const uint32_t rocks = gk_upload_mesh(r,&builder,1.1f);
#ifdef SNDMIN_VALLEY
    valley_audio audio=valley_audio_init(shared_journal);
#endif
    double measured[6] = {0}, minimum_ms = HUGE_VAL, maximum_ms = 0;
    uint32_t measurements = 0;
    while (vkmin_running(gpu)) {
        const vkmin_frame frame = vkmin_frame_begin(gpu,NULL);
        const valley_camera camera_state=valley_camera_at(keys,key_count,frame.index);
        const vec3 eye=camera_state.eye, target=camera_state.target;
#ifdef SNDMIN_VALLEY
        /* Coarse solid ground columns follow the authored terrain, not a scene graph. */
        sndmin_box acoustic_boxes[64];
        for(unsigned k=0;k<64;++k) {
            const vec4 bound=nodes[21+k].bounds;
            const float top=fmaxf(minimum-9,bound.y-bound.w*.35f);
            acoustic_boxes[k]=(sndmin_box){{bound.x-cell*(float)chunk*.5f,minimum-10,bound.z-cell*(float)chunk*.5f},
                {bound.x+cell*(float)chunk*.5f,top,bound.z+cell*(float)chunk*.5f},0};
        }
        /* Rendering may select isolated frames. Audio still receives every
         * preceding game tick, using the same pure route evaluation. */
        while(audio.frames<=frame.index) {
            const valley_camera step=valley_camera_at(keys,key_count,audio.frames);
            valley_audio_frame(&audio,audio.frames,step.eye,step.target,step.velocity,acoustic_boxes,64,river);
        }
#endif
        const vec4 camera = {eye.x,eye.y,eye.z,1};
        uint32_t selected[64];
        const uint32_t terrain_count = vkmin_terrain_select(nodes,camera,1.8f,small,selected,64);
        Instance instances[64];
        for (uint32_t k = 0; k < terrain_count; ++k) instances[k] = (Instance){.transform = vkmin_mat4_identity(),
            .prev_transform = vkmin_mat4_identity(), .bounds = nodes[selected[k]].bounds, .mesh = terrain_mesh+selected[k],
            .material = mat, .bone_offset = VKMIN_NONE, .id = k+1};
        Scatter scatter[5] = {
            {.origin_cell = {0,0,0,patches ? 1.05f : .55f}, .scale = {.75f,1.3f,small ? .018f : .9f,32},
             .data = {grass,mat+1,grass_density,13}, .grid = {patches ? 64u : 120u,patches ? 64u : 120u,VKMIN_INST_GRASS,0}},
            {.origin_cell = {0,0,0,.9f}, .scale = {.3f,.7f,.85f,30},
             .data = {flowers,mat+2,flowers_density,53}, .grid = {27,27,VKMIN_INST_LEAF,0}},
            {.origin_cell = {0,0,0,6}, .scale = {.3f,1.1f,.8f,80},
             .data = {rocks,mat+3,rocks_density,37}, .grid = {14,14,0,0}},
            {.origin_cell = {0,0,0,8}, .scale = {.8f,1.5f,.8f,120},
             .data = {trunk,mat+4,tree_density,7}, .grid = {20,20,0,0}, .foliage = {leaves,mat+5,tree_atlas,8}},
            {.origin_cell = {0,0,0,4}, .scale = {.65f,1.4f,.65f,55},
             .data = {shrub,mat+5,tree_density,87}, .grid = {12,12,VKMIN_INST_LEAF,0}}
        };
        for (uint32_t k = 0; k < 5; ++k) {
            const float spacing = scatter[k].origin_cell.w;
            scatter[k].origin_cell.x = (floorf(eye.x/spacing)-(float)scatter[k].grid.x*.5f)*spacing;
            scatter[k].origin_cell.z = (floorf(eye.z/spacing)-(float)scatter[k].grid.y*.5f)*spacing;
        }
        /* Hours since midnight -> sun position; the 120-second route covers
         * 07:00..10:00. This schedule belongs to the demo, not the sky model. */
        const float hour = 7.0f+3*fminf((float)frame.index/7199,1);
        const vec4 sun_direction = vkmin_sun_direction(hour);
        const Light sun = gk_sun((vec3){-sun_direction.x,-sun_direction.y,-sun_direction.z},4.5f);
        const vkr_stats stats = vkr_get_stats(r);
        char overlay[512];
        if (options.headless) snprintf(overlay,sizeof overlay,"VALLEY / frame %u",frame.index);
        else snprintf(overlay,sizeof overlay,"VALLEY / summer morning\nscatter %.2f ms  grass %.2f ms\nsky %.2f ms  water %.2f ms  TAA %.2f ms",
            stats.outside_ms[0],stats.outside_ms[1],stats.outside_ms[2],stats.outside_ms[3],stats.outside_ms[4]);
        vkr_frame(r,&(vkr_frame_desc){.view = vkmin_mat4_look_at(eye,target,(vec3){0,1,0}),
            .proj = vkmin_mat4_perspective(1.05f,frame.aspect,.2f,small ? 1000 : 2600), .camera_pos = camera,
            .near = .2f, .far = small ? 1000 : 2600, .instances = instances, .instance_count = terrain_count,
            .lights = &sun, .light_count = 1, .frame = frame, .outdoor = &outside,
            .scatter = scatter, .scatter_count = 5, .overlay_text = overlay});
        const vkr_stats measured_frame = vkr_get_stats(r);
        if (frame.index >= 8 && measured_frame.frame_ms > 0) {
            for (int k = 0; k < 5; ++k) measured[k] += measured_frame.outside_ms[k];
            measured[5] += measured_frame.frame_ms; ++measurements;
            minimum_ms = fmin(minimum_ms,measured_frame.frame_ms); maximum_ms = fmax(maximum_ms,measured_frame.frame_ms);
        }
        vkmin_frame_end(gpu);
        if (!options.headless && frame.index >= 7199) break;
    }
    (void)vkr_finish(r);
    for (int k = 1; k < argc; ++k) if (!strcmp(argv[k],"--timings")) {
        if (!measurements) printf("GPU timings: no samples after warm-up; render at least 12 consecutive frames\n");
        else printf("GPU timestamps: %u samples, mean %.3f ms (min %.3f, max %.3f); scatter %.3f, grass %.3f, sky %.3f, water %.3f, TAA %.3f ms\n",
            measurements,measured[5]/measurements,minimum_ms,maximum_ms,measured[0]/measurements,measured[1]/measurements,
            measured[2]/measurements,measured[3]/measurements,measured[4]/measurements);
        printf("grass geometry: %u blades per patch, %u candidate patches; GPU cull retains fixed command slots\n",
            blade_count,patches ? 4096u : 14400u);
    }
#ifdef SNDMIN_VALLEY
    if(!sndmin_render(audio.ctx,audio.frames,audio_out,audio_png)) gk_die("audio render failed");
    sndmin_dump(audio.ctx,stdout); sndmin_shutdown(audio.ctx);
#endif
    vkr_shutdown(r); vkmin_shutdown(gpu);
#ifdef SNDMIN_VALLEY
    if(shared_journal&&fclose(shared_journal)!=0) gk_die("shared journal close failed");
#endif
    return 0;
}
