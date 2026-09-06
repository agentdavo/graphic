/* Private diagnostic implementation, included after frame submission helpers.
 * Inspection is opt-in: temporary staging allocations never enter the journal.
 * Raw files retain exact texels; tools/inspect_frame.py supplies previews. */
static void inspect_event(const vkmin_ctx *c, FILE *out, uint32_t event,
                          const record_header *rh, const uint8_t *hdr, const uint8_t *data) {
    static const char *const names[] = {"invalid", "make_buffer", "free_buffer", "buffer_upload",
        "make_image", "free_image", "image_upload", "index", "register", "make_pipeline",
        "frame_begin", "frame_end", "ring_alloc", "barrier", "fill", "copy_to_ring",
        "pass_begin", "pass_end", "viewport", "depth_bias", "draw", "draw_indirect",
        "dispatch", "timestamp", "pick", "replace_pipeline"};
    uint32_t frame = c->frame_index;
    if (rh->op == OP_FRAME_BEGIN && rh->hdr_bytes == sizeof(rec_frame)) {
        rec_frame r; memcpy(&r, hdr, sizeof r); frame = r.frame_index;
    }
    fprintf(out, "%u\t%u\t%s\t", event, frame, names[rh->op]);
    if (rh->op == OP_PASS_BEGIN && rh->hdr_bytes >= sizeof(rec_pass)) {
        rec_pass r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "color=%u depth=%u extra=%u,%u area=%d,%d,%d,%d", r.color, r.depth,
                r.extra[0], r.extra[1], r.x, r.y, r.w, r.h);
        if (rh->hdr_bytes == sizeof(rec_named_pass)) {
            rec_named_pass n; memcpy(&n, hdr, sizeof n);
            for (size_t i = 0; i < sizeof n.label; ++i)
                if ((unsigned char)n.label[i] < 32 && n.label[i]) n.label[i] = ' ';
            fprintf(out, " label=%.*s", (int)sizeof n.label, n.label);
        }
    } else if ((rh->op == OP_DRAW || rh->op == OP_DISPATCH) && rh->hdr_bytes == sizeof(rec_draw)) {
        rec_draw r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "pipeline=%u counts=%u,%u,%u push_bytes=%u", r.pipe, r.a, r.b, r.cnt, r.push_bytes);
        if (handle_index(r.pipe) < VKMIN_MAX_PIPES)
            fprintf(out, " label=%s", c->pipes[handle_index(r.pipe)].label);
    } else if (rh->op == OP_DRAW_INDIRECT && rh->hdr_bytes == sizeof(rec_indirect)) {
        rec_indirect r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "pipeline=%u indices=%u commands=%u offset=%llu counts=%u offset=%llu max=%u push_bytes=%u",
                r.pipe, r.indices, r.cmds, (unsigned long long)r.cmd_offset, r.counts,
                (unsigned long long)r.count_offset, r.max_draws, r.push_bytes);
    } else if (rh->op == OP_BARRIER && rh->hdr_bytes == sizeof(rec_barrier)) {
        rec_barrier r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "flags=%u images=%u", r.flags, r.image_count);
        if (r.image_count <= VKMIN_MAX_IMAGES && (uint64_t)r.image_count * sizeof(vkmin_transition) == rh->data_bytes) {
            for (uint32_t i = 0; i < r.image_count; ++i) {
                vkmin_transition t; memcpy(&t, data + i * sizeof t, sizeof t);
                fprintf(out, " image=%u use=%u", t.image.id, (unsigned)t.use);
            }
        }
    } else if ((rh->op == OP_MAKE_PIPELINE || rh->op == OP_REPLACE_PIPELINE) && rh->hdr_bytes == sizeof(rec_pipe)) {
        rec_pipe r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "pipeline=%u push_bytes=%u shaders=%u,%u,%u label=%.*s", r.result, r.push_size,
                r.vs_bytes, r.fs_bytes, r.cs_bytes, VKMIN_LABEL, r.label);
    } else if (rh->op == OP_MAKE_BUFFER && rh->hdr_bytes == sizeof(rec_buffer)) {
        rec_buffer r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "buffer=%u bytes=%llu label=%.*s", r.result, (unsigned long long)r.size, VKMIN_LABEL, r.label);
    } else if (rh->op == OP_MAKE_IMAGE && rh->hdr_bytes == sizeof(rec_image)) {
        rec_image r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "image=%u size=%dx%d format=%u label=%.*s", r.result, r.w, r.h, r.format, VKMIN_LABEL, r.label);
    } else if ((rh->op == OP_BUFFER_UPLOAD || rh->op == OP_IMAGE_UPLOAD || rh->op == OP_RING_ALLOC || rh->op == OP_FRAME_END)
               && rh->hdr_bytes == sizeof(rec_upload)) {
        rec_upload r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "resource=%u mip=%u offset=%llu", r.id, r.mip, (unsigned long long)r.offset);
    } else if ((rh->op == OP_FREE_BUFFER || rh->op == OP_FREE_IMAGE || rh->op == OP_TIMESTAMP) && rh->hdr_bytes == sizeof(uint32_t)) {
        uint32_t value; memcpy(&value, hdr, sizeof value);
        fprintf(out, "value=%u", value);
    } else if ((rh->op == OP_INDEX || rh->op == OP_REGISTER) && rh->hdr_bytes == sizeof(rec_draw)) {
        rec_draw r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "image=%u value_a=%u value_b=%u", r.pipe, r.a, r.b);
    } else if (rh->op == OP_VIEWPORT && rh->hdr_bytes == sizeof(rec_pass)) {
        rec_pass r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "area=%d,%d,%d,%d", r.x, r.y, r.w, r.h);
    } else if ((rh->op == OP_FILL || rh->op == OP_COPY_TO_RING) && rh->hdr_bytes == sizeof(rec_indirect)) {
        rec_indirect r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "buffer=%u offset=%llu bytes=%llu value=%u", r.cmds,
                (unsigned long long)r.cmd_offset, (unsigned long long)r.count_offset, r.max_draws);
    } else if (rh->op == OP_PICK && rh->hdr_bytes == sizeof(rec_pick)) {
        rec_pick r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "image=%u pixel=%d,%d result=%u", r.image, r.x, r.y, r.result);
    }
    fprintf(out, " header_bytes=%u data_bytes=%u relocations=%u\n", rh->hdr_bytes, rh->data_bytes, rh->reloc_count);
}

static bool inspect_images(vkmin_ctx *c, const char *directory) {
    char path[1024];
    int n = snprintf(path, sizeof path, "%s/images.tsv", directory);
    if (n < 0 || (size_t)n >= sizeof path) return false;
    FILE *manifest = fopen(path, "w");
    if (!manifest) return false; /* Caller creates the directory. */
    fprintf(manifest, "slot\tlabel\twidth\theight\tformat\tfile\n");
    timeline_wait(c, c->timeline_value);
    bool ok = true;
    for (uint32_t i = 0; i < VKMIN_MAX_IMAGES && ok; ++i) {
        image_slot *s = &c->images[i];
        if (!s->used || !c->inspect_targets[i] || s->use == VKMIN_USE_UNDEFINED) continue;
        format_info fi = {0};
        int format = -1;
        for (int k = 0; k < VKMIN_FMT_NONE; ++k) {
            const format_info candidate = format_lookup((vkmin_format)k);
            if (candidate.vk == s->format) { fi = candidate; format = k; break; }
        }
        if (format < 0 || fi.block_dim != 1) { ok = false; break; }
        const size_t bytes = mip_bytes(fi, s->w, s->h);
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        create_backing_buffer(c, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &buffer, &memory, NULL, "inspection.readback");
        const vkmin_use previous = s->use;
        const VkCommandBuffer cmd = imm_begin(c);
        cmd_transition(cmd, s, VKMIN_USE_TRANSFER_SRC, false);
        const VkBufferImageCopy region = {.imageSubresource = {.aspectMask = s->aspect, .layerCount = 1},
                                          .imageExtent = {s->w, s->h, 1}};
        vkCmdCopyImageToBuffer(cmd, s->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
        const VkMemoryBarrier2 host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT, .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT};
        const VkDependencyInfo dep = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &host};
        vkCmdPipelineBarrier2(cmd, &dep);
        cmd_transition(cmd, s, previous, false);
        imm_end(c);
        void *mapped = NULL;
        VK_CHECK(vkMapMemory(c->dev, memory, 0, bytes, 0, &mapped));
        n = snprintf(path, sizeof path, "%s/image_%u.raw", directory, i);
        FILE *file = n >= 0 && (size_t)n < sizeof path ? fopen(path, "wb") : NULL;
        if (file) { ok = fwrite(mapped, 1, bytes, file) == bytes; if (fclose(file)) ok = false; }
        else ok = false;
        vkUnmapMemory(c->dev, memory);
        vkDestroyBuffer(c->dev, buffer, NULL);
        vkFreeMemory(c->dev, memory, NULL);
        char label[VKMIN_LABEL]; memcpy(label, s->label, sizeof label);
        for (size_t j = 0; j < sizeof label; ++j) if ((unsigned char)label[j] < 32 && label[j]) label[j] = ' ';
        fprintf(manifest, "%u\t%.*s\t%u\t%u\t%d\timage_%u.raw\n", i, VKMIN_LABEL, label, s->w, s->h, format, i);
    }
    if (ferror(manifest)) ok = false;
    if (fclose(manifest)) ok = false;
    n = snprintf(path, sizeof path, "%s/resources.txt", directory);
    FILE *resources = n >= 0 && (size_t)n < sizeof path ? fopen(path, "w") : NULL;
    if (resources) { vkmin_dump(c, resources); if (fclose(resources)) ok = false; }
    else ok = false;
    return ok;
}
