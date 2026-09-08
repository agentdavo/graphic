/* Private diagnostic implementation, included after frame submission helpers.
 * Inspection is opt-in: temporary staging allocations never enter the journal.
 * Raw files retain exact texels; tools/inspect_frame.py supplies previews. */
/* Address candidates are explicitly labelled: untyped push bytes cannot prove a
 * shader read. Logical identities survive independent replay address spaces. */
static void inspect_push_refs(const vkmin_ctx *c, FILE *out, const uint8_t *data, uint32_t bytes) {
    fprintf(out, " refs=");
    for (uint32_t off = 0; off + 8 <= bytes; off += 8) {
        uint64_t address; memcpy(&address, data + off, 8);
        for (uint32_t i = 0; i < VKMIN_MAX_BUFFERS; ++i) {
            const buffer_slot *b = &c->buffers[i];
            const uint64_t base = c->arena_addr + b->offset;
            if (b->used && address >= base && address - base < b->size)
                fprintf(out, "%u:buffer:%u:%llu,", off, handle_make(i, b->gen), (unsigned long long)(address-base));
        }
        if (address >= c->ring_addr && address - c->ring_addr < c->ring_cap)
            fprintf(out, "%u:ring:0:%llu,", off, (unsigned long long)(address-c->ring_addr));
    }
}

static void inspect_event(const vkmin_ctx *c, FILE *out, uint32_t event,
                          const record_header *rh, const uint8_t *hdr, const uint8_t *data) {
    /* Indexed by the opcode itself, from the same list that declares the enum,
     * so a name cannot come to sit against the wrong op however the list is edited. */
    static const char *const names[] = {
        [OP_INVALID] = "invalid",
#define VKMIN_OP_NAME(op, name) [OP_##op] = name,
        VKMIN_OP_LIST(VKMIN_OP_NAME)
#undef VKMIN_OP_NAME
    };
    _Static_assert(sizeof names / sizeof *names == OP_COUNT, "one name per opcode");
    /* rh comes from a journal, which is untrusted input. The replay loop
     * rejects an out-of-range opcode before it reaches us, but that check is
     * three hundred lines away behind a macro and -fanalyzer cannot see it, so
     * at -O0 it reports an attacker-controlled array index here and the build
     * fails. Bound it locally: the lookup is then correct on its own terms
     * whoever calls it, which is cheaper than teaching every reader that the
     * safety lives somewhere else. */
    const uint32_t op = rh->op < (uint32_t)OP_COUNT ? rh->op : (uint32_t)OP_INVALID;
    uint32_t frame = c->frame_index;
    if (rh->op == OP_FRAME_BEGIN && rh->hdr_bytes == sizeof(rec_frame)) {
        rec_frame r; memcpy(&r, hdr, sizeof r); frame = r.frame_index;
    }
    fprintf(out, "%u\t%u\t%s\t", event, frame, names[op]);
    if (rh->op == OP_PASS_BEGIN && rh->hdr_bytes >= sizeof(rec_pass)) {
        rec_pass r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "color=%u depth=%u extra=%u,%u clear_color=%u clear_depth=%u area=%d,%d,%d,%d", r.color, r.depth,
                r.extra[0], r.extra[1], (unsigned)r.clear_color, (unsigned)r.clear_depth, r.x, r.y, r.w, r.h);
        if (rh->hdr_bytes == sizeof(rec_named_pass) || rh->hdr_bytes == REC_PASS_OLD) {
            rec_named_pass n = {0}; memcpy(&n, hdr, rh->hdr_bytes);
            for (size_t i = 0; i < sizeof n.label; ++i) {
                if ((unsigned char)n.label[i] < 32 && n.label[i]) n.label[i] = ' ';
            }
            fprintf(out, " label=%.*s resolves=%u,%u,%u,%u depth_mode=%u raster_samples=%u", (int)sizeof n.label, n.label,
                    n.resolves[0], n.resolves[1], n.resolves[2], n.resolves[3], n.depth_mode, n.raster_samples);
        }
    } else if ((rh->op == OP_DRAW || rh->op == OP_DISPATCH) && rh->hdr_bytes == sizeof(rec_draw)) {
        rec_draw r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "pipeline=%u counts=%u,%u,%u push_bytes=%u", r.pipe, r.a, r.b, r.cnt, r.push_bytes);
        if (handle_index(r.pipe) < VKMIN_MAX_PIPES) {
            fprintf(out, " label=%s", c->pipes[handle_index(r.pipe)].label);
        }
        if (r.push_bytes == rh->data_bytes && r.push_bytes <= VKMIN_PUSH_BYTES) {
            fprintf(out, " push_hex=");
            for (uint32_t i = 0; i < r.push_bytes; ++i) fprintf(out, "%02x", (unsigned)data[i]);
            inspect_push_refs(c, out, data, r.push_bytes);
        }
    } else if (rh->op == OP_DRAW_INDIRECT && rh->hdr_bytes == sizeof(rec_indirect)) {
        rec_indirect r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "pipeline=%u indices=%u commands=%u command_offset=%llu counts=%u count_offset=%llu max=%u push_bytes=%u",
                r.pipe, r.indices, r.cmds, (unsigned long long)r.cmd_offset, r.counts,
                (unsigned long long)r.count_offset, r.max_draws, r.push_bytes);
        if (r.push_bytes + 8u == rh->data_bytes && r.push_bytes <= VKMIN_PUSH_BYTES) {
            fprintf(out, " push_hex=");
            for (uint32_t i = 0; i < r.push_bytes; ++i) fprintf(out, "%02x", (unsigned)data[i]);
            inspect_push_refs(c, out, data, r.push_bytes);
        }
    } else if (rh->op == OP_BARRIER && rh->hdr_bytes == sizeof(rec_barrier)) {
        rec_barrier r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "flags=%u images=%u", r.flags, r.image_count);
        if (r.image_count <= VKMIN_MAX_IMAGES && (uint64_t)r.image_count * sizeof(vkmin_transition) == rh->data_bytes) {
            for (uint32_t i = 0; i < r.image_count; ++i) {
                vkmin_transition t; memcpy(&t, data + i * sizeof t, sizeof t);
                fprintf(out, " image=%u use=%u", t.image.id, (unsigned)t.use);
            }
        }
    } else if ((rh->op == OP_MAKE_PIPELINE || rh->op == OP_REPLACE_PIPELINE) && (rh->hdr_bytes == sizeof(rec_pipe) || rh->hdr_bytes == REC_PIPE_OLD)) {
        rec_pipe r = {0}; memcpy(&r, hdr, rh->hdr_bytes);
        fprintf(out, "pipeline=%u push_bytes=%u shaders=%u,%u,%u label=%.*s", r.result, r.push_size,
                r.vs_bytes, r.fs_bytes, r.cs_bytes, VKMIN_LABEL, r.label);
        fprintf(out, " samples=%u alpha_to_coverage=%u depth_test=%u depth_attachment=%u", r.samples ? r.samples : 1, r.alpha_to_coverage, r.depth & 1u,
                (unsigned)((r.depth & 1u) || (c->journal_version < 8 ? r.color_format == VKMIN_FMT_RGBA8_UNORM : (r.depth & 2u) != 0)));
    } else if (rh->op == OP_MAKE_BUFFER && rh->hdr_bytes == sizeof(rec_buffer)) {
        rec_buffer r; memcpy(&r, hdr, sizeof r);
        fprintf(out, "buffer=%u bytes=%llu label=%.*s", r.result, (unsigned long long)r.size, VKMIN_LABEL, r.label);
    } else if (rh->op == OP_MAKE_IMAGE && (rh->hdr_bytes == sizeof(rec_image) || rh->hdr_bytes == REC_IMAGE_OLD)) {
        rec_image r = {0}; memcpy(&r, hdr, rh->hdr_bytes);
        fprintf(out, "image=%u size=%dx%d format=%u label=%.*s", r.result, r.w, r.h, r.format, VKMIN_LABEL, r.label);
        fprintf(out, " samples=%u render_to_single=%u", r.samples ? r.samples : 1, r.to_single);
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

static bool inspect_buffers(vkmin_ctx *c, const char *directory) {
    char path[1024];
    int n = snprintf(path, sizeof path, "%s/buffers.tsv", directory);
    FILE *manifest = n >= 0 && (size_t)n < sizeof path ? fopen(path, "w") : NULL;
    if (!manifest) return false;
    fprintf(manifest, "id\tkind\tlabel\toffset\tsize\tfile\n");
    bool ok = true;
    for (uint32_t i = 0; i <= VKMIN_MAX_BUFFERS && ok; ++i) {
        const bool ring = i == VKMIN_MAX_BUFFERS;
        const buffer_slot *b = ring ? NULL : &c->buffers[i];
        if (!ring && !b->used) continue;
        const VkDeviceSize offset = ring ? c->last_slot*c->ring_region : b->offset;
        const VkDeviceSize size = ring ? c->ring_head[c->last_slot] : b->size;
        if (!size) continue;
        const uint32_t id = ring ? 0 : handle_make(i, b->gen);
        char label[VKMIN_LABEL]; snprintf(label, sizeof label, "%s", ring ? "frame ring" : b->label);
        for (size_t j = 0; j < sizeof label; ++j) if (label[j] && (unsigned char)label[j] < 32) label[j] = ' ';
        /* A large resource is explicit in the manifest, never silently truncated. */
        fprintf(manifest, "%u\t%s\t%s\t%llu\t%llu\t", id, ring ? "ring" : "buffer", label,
                (unsigned long long)(ring ? offset : 0), (unsigned long long)size);
        if (size > (64u << 20)) { fprintf(manifest, "\n"); continue; }
        VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE;
        create_backing_buffer(c, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging, &memory, NULL, "inspection.buffer");
        const VkCommandBuffer cmd = imm_begin(c);
        VkMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT};
        const VkDependencyInfo dep = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &barrier};
        vkCmdPipelineBarrier2(cmd, &dep);
        const VkBufferCopy region = {.srcOffset = offset, .size = size};
        vkCmdCopyBuffer(cmd, ring ? c->ring_buf : c->arena_buf, staging, 1, &region);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        vkCmdPipelineBarrier2(cmd, &dep); imm_end(c);
        void *mapped = NULL; VK_CHECK_CTX(c, vkMapMemory(c->dev, memory, 0, size, 0, &mapped));
        n = snprintf(path, sizeof path, "%s/buffer_%u.raw", directory, i);
        FILE *file = n >= 0 && (size_t)n < sizeof path ? fopen(path, "wb") : NULL;
        if (file) { ok = fwrite(mapped, 1, (size_t)size, file) == (size_t)size; if (fclose(file)) ok = false; }
        else ok = false;
        vkUnmapMemory(c->dev, memory); vkDestroyBuffer(c->dev, staging, NULL); vkFreeMemory(c->dev, memory, NULL);
        fprintf(manifest, "buffer_%u.raw\n", i);
    }
    if (ferror(manifest)) ok = false;
    if (fclose(manifest)) ok = false;
    n = snprintf(path, sizeof path, "%s/device.tsv", directory);
    FILE *device = n >= 0 && (size_t)n < sizeof path ? fopen(path, "w") : NULL;
    if (!device) return false;
    VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
    vkGetPhysicalDeviceProperties2(c->phys, &props);
    fprintf(device, "device\tvendor_id\tdevice_id\tapi_version\tdriver_version\tdriver_name\tdriver_info\tbuild\n");
    fprintf(device, "%s\t%u\t%u\t%u\t%u\t%s\t%s\t%s\n", props.properties.deviceName,
        props.properties.vendorID, props.properties.deviceID, props.properties.apiVersion,
        props.properties.driverVersion, driver.driverName, driver.driverInfo, c->debug ? "Debug" : "Release");
    if (ferror(device)) ok = false;
    if (fclose(device)) ok = false;
    return ok;
}

static bool inspect_images(vkmin_ctx *c, const char *directory) {
    char path[1024];
    int n = snprintf(path, sizeof path, "%s/images.tsv", directory);
    if (n < 0 || (size_t)n >= sizeof path) return false;
    FILE *manifest = fopen(path, "w");
    if (!manifest) return false; /* Caller creates the directory. */
    fprintf(manifest, "slot\tlabel\twidth\theight\tformat\tfile\tid\tsamples\n");
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
        char label[VKMIN_LABEL]; memcpy(label, s->label, sizeof label);
        for (size_t j = 0; j < sizeof label; ++j) if ((unsigned char)label[j] < 32 && label[j]) label[j] = ' ';
        if (s->samples > 1) {
            /* Vulkan forbids image-to-buffer copies of multisample images.
             * Expose their identity without pretending resolved bytes are samples. */
            fprintf(manifest, "%u\t%.*s\t%u\t%u\t%d\t\t%u\t%u\n", i, VKMIN_LABEL, label,
                    s->w, s->h, format, handle_make(i, s->gen), s->samples);
            continue;
        }
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
        VK_CHECK_CTX(c, vkMapMemory(c->dev, memory, 0, bytes, 0, &mapped));
        n = snprintf(path, sizeof path, "%s/image_%u.raw", directory, i);
        FILE *file = n >= 0 && (size_t)n < sizeof path ? fopen(path, "wb") : NULL;
        if (file) { ok = fwrite(mapped, 1, bytes, file) == bytes; if (fclose(file)) ok = false; }
        else ok = false;
        vkUnmapMemory(c->dev, memory);
        vkDestroyBuffer(c->dev, buffer, NULL);
        vkFreeMemory(c->dev, memory, NULL);

        fprintf(manifest, "%u\t%.*s\t%u\t%u\t%d\timage_%u.raw\t%u\t1\n", i, VKMIN_LABEL, label, s->w, s->h, format, i, handle_make(i, s->gen));
    }
    if (ferror(manifest)) ok = false;
    if (fclose(manifest)) ok = false;
    n = snprintf(path, sizeof path, "%s/resources.txt", directory);
    FILE *resources = n >= 0 && (size_t)n < sizeof path ? fopen(path, "w") : NULL;
    if (resources) { vkmin_dump(c, resources); if (fclose(resources)) ok = false; }
    else ok = false;
    return ok && inspect_buffers(c, directory);
}
