#include "video_denoising.h"
#include <stddef.h>   // for NULL
#include <stdlib.h>   // for malloc and free
#include <stdio.h>    // for FILE, printf, snprintf, fopen, fclose

Image* denoise_frame(FrameBuffer* buffer, const DenoisingParams* params) {
    printf("Starting denoise_frame with buffer=%p, params=%p\n", (void*)buffer, (void*)params);
    
    if (!buffer) {
        printf("Error: NULL buffer\n");
        return NULL;
    }
    if (!params) {
        printf("Error: NULL params\n");
        return NULL;
    }
    printf("Buffer capacity: %d, size: %d, temporal_radius: %d\n", 
           buffer->capacity, buffer->size, params->temporal_radius);
    
    if (params->temporal_radius < 0 || params->temporal_radius * 2 + 1 > buffer->capacity) {
        printf("Error: Invalid temporal radius %d for buffer capacity %d\n", 
               params->temporal_radius, buffer->capacity);
        return NULL;
    }
    
    if (params->block_size <= 0 || params->search_radius <= 0) {
        printf("Error: Invalid block_size=%d or search_radius=%d\n", 
               params->block_size, params->search_radius);
        return NULL;
    }
    
    int center_idx = buffer->current - params->temporal_radius;
    if (center_idx < 0) center_idx += buffer->capacity;
    
    printf("Allocating aligned_frames array for %d frames\n", 2*params->temporal_radius + 1);
    Image** aligned_frames = malloc(sizeof(Image*) * (2*params->temporal_radius + 1));
    if (!aligned_frames) {
        printf("Failed to allocate aligned_frames array\n");
        return NULL;
    }
    
    for (int i = 0; i < 2*params->temporal_radius + 1; i++) {
        aligned_frames[i] = NULL;
    }
    
    printf("Setting center frame at index %d\n", params->temporal_radius);
    aligned_frames[params->temporal_radius] = buffer->frames[center_idx];
    if (!aligned_frames[params->temporal_radius]) {
        printf("Center frame is NULL\n");
        free(aligned_frames);
        return NULL;
    }
    
    // Initialize block matching parameters
    BlockMatchingParams* bm_params = create_block_matching_params(1);
    if (!bm_params) {
        printf("Failed to create block matching params\n");
        free(aligned_frames);
        return NULL;
    }
    
    printf("Setting block matching parameters: block_size=%d, search_radius=%d\n", 
           params->block_size, params->search_radius);
    if (params->block_size <= 0 || params->search_radius <= 0) {
        printf("Error: Invalid block matching parameters\n");
        free_block_matching_params(bm_params);
        free(aligned_frames);
        return NULL;
    }
    bm_params->tile_sizes[0] = params->block_size;
    bm_params->search_radii[0] = params->search_radius;
    
    // Align neighboring frames to center frame
    for (int offset = -params->temporal_radius; offset <= params->temporal_radius; offset++) {
        if (offset == 0) continue;
        
        printf("Processing frame offset %d\n", offset);
        int frame_idx = (center_idx + offset + buffer->capacity) % buffer->capacity;
        printf("Accessing frame at buffer index %d\n", frame_idx);
        
        if (!buffer->frames[frame_idx]) {
            printf("Frame at index %d is NULL\n", frame_idx);
            free_block_matching_params(bm_params);
            free(aligned_frames);
            return NULL;
        }
        
        // Compute optical flow
        printf("Initializing block matching for reference frame\n");
        ImagePyramid* ref_pyramid = init_block_matching(buffer->frames[center_idx], bm_params);
        if (!ref_pyramid) {
            printf("Failed to initialize block matching\n");
            free_block_matching_params(bm_params);
            free(aligned_frames);
            return NULL;
        }
        
        AlignmentMap* flow = align_image_block_matching(buffer->frames[frame_idx], ref_pyramid, bm_params);
        if (!flow) {
            free_image_pyramid(ref_pyramid);
            free_block_matching_params(bm_params);
            free(aligned_frames);
            return NULL;
        }
        
        // Warp frame
        Image* warped = warp_image(buffer->frames[frame_idx], flow);
        if (!warped) {
            free_alignment_map(flow);
            free_image_pyramid(ref_pyramid);
            free_block_matching_params(bm_params);
            free(aligned_frames);
            return NULL;
        }
        
        aligned_frames[params->temporal_radius + offset] = warped;
        
        // Cleanup
        free_image_pyramid(ref_pyramid);
        free_alignment_map(flow);
    }
    
    // Perform temporal averaging
    Image* denoised = temporal_average(aligned_frames, 2*params->temporal_radius + 1);
    
    // Cleanup
    free_block_matching_params(bm_params);
    for (int i = 0; i < 2*params->temporal_radius + 1; i++) {
        if (i != params->temporal_radius) { // Don't free center frame
            free_image(aligned_frames[i]);
        }
    }
    free(aligned_frames);
    
    return denoised;
} 

static char* current_frame_path = NULL;
static int frame_counter = 0;

Image* load_next_frame(const char* input_pattern, int frame_idx) {
    if (!input_pattern) {
        printf("Error: NULL input pattern\n");
        return NULL;
    }
    
    char frame_path[256];
    snprintf(frame_path, sizeof(frame_path), input_pattern, frame_idx);
    
    FILE* f = fopen(frame_path, "r");
    if (!f) {
        printf("Error: Cannot open file %s\n", frame_path);
        return NULL;
    }
    fclose(f);
    
    Image* img = load_image(frame_path);
    if (!img) {
        printf("Error: Failed to load image %s\n", frame_path);
        return NULL;
    }
    
    return img;
} 