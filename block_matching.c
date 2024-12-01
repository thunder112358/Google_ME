#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdio.h>
#include "block_matching.h"

// Helper function declarations
static Image* downsample_image(const Image* img, int factor);
static AlignmentMap* align_on_level(const Image* ref_level, const Image* alt_level, 
                                  const BlockMatchingParams* params, int level_idx,
                                  const AlignmentMap* prev_alignments);
static AlignmentMap* upsample_alignments(const Image* ref_level, const Image* alt_level,
                                       const AlignmentMap* prev_alignments,
                                       int upsampling_factor, int tile_size, int prev_tile_size);
static void local_search(const Image* ref_level, const Image* alt_level,
                        int tile_size, int search_radius,
                        AlignmentMap* alignments, int distance_metric);

// Implementation of core functions
ImagePyramid* init_block_matching(const Image* ref_img, const BlockMatchingParams* params) {
    // Add parameter validation
    if (!ref_img) {
        printf("Error: ref_img is NULL\n");
        return NULL;
    }
    if (!params) {
        printf("Error: params is NULL\n");
        return NULL;
    }
    if (params->num_levels <= 0) {
        printf("Error: params->num_levels must be positive (got %d)\n", params->num_levels);
        return NULL;
    }

    ImagePyramid* pyramid = create_image_pyramid(params->num_levels);
    if (!pyramid) {
        printf("Failed to create image pyramid\n");
        return NULL;
    }

    // Create first level (original resolution or initial downsampling)
    pyramid->levels[0] = downsample_image(ref_img, params->factors[0]);
    if (!pyramid->levels[0]) {
        printf("Failed to create first pyramid level with factor %d\n", params->factors[0]);
        free_image_pyramid(pyramid);
        return NULL;
    }

    // Create subsequent levels
    for (int i = 1; i < params->num_levels; i++) {
        pyramid->levels[i] = downsample_image(pyramid->levels[i-1], params->factors[i]);
        if (!pyramid->levels[i]) {
            printf("Failed to create pyramid level %d with factor %d\n", i, params->factors[i]);
            free_image_pyramid(pyramid);
            return NULL;
        }
    }

    return pyramid;
}

AlignmentMap* align_image_block_matching(const Image* img, const ImagePyramid* reference_pyramid,
                                       const BlockMatchingParams* params) {
    // Create pyramid for the image to be aligned
    ImagePyramid* alt_pyramid = init_block_matching(img, params);
    if (!alt_pyramid) return NULL;

    AlignmentMap* alignments = NULL;
    
    // Process from coarsest to finest level
    for (int level = params->num_levels - 1; level >= 0; level--) {
        AlignmentMap* level_alignments = align_on_level(
            reference_pyramid->levels[level],
            alt_pyramid->levels[level],
            params,
            level,
            alignments
        );

        // Free previous level alignments
        if (alignments) {
            free_alignment_map(alignments);
        }
        alignments = level_alignments;
    }

    free_image_pyramid(alt_pyramid);
    return alignments;
}

static Image* downsample_image(const Image* img, int factor) {
    if (factor <= 0) return NULL;
    if (factor == 1) {
        // Create a copy of the image
        Image* copy = create_image(img->height, img->width, img->channels);
        if (!copy) return NULL;
        memcpy(copy->data, img->data, sizeof(pixel_t) * img->height * img->width * img->channels);
        return copy;
    }

    int new_height = img->height / factor;
    int new_width = img->width / factor;
    Image* downsampled = create_image(new_height, new_width, img->channels);
    if (!downsampled) return NULL;

    // Simple box filter downsampling for each channel
    for (int c = 0; c < img->channels; c++) {
        for (int y = 0; y < new_height; y++) {
            for (int x = 0; x < new_width; x++) {
                float sum = 0.0f;
                for (int ky = 0; ky < factor; ky++) {
                    for (int kx = 0; kx < factor; kx++) {
                        int orig_y = y * factor + ky;
                        int orig_x = x * factor + kx;
                        sum += img->data[(orig_y * img->width + orig_x) * img->channels + c];
                    }
                }
                downsampled->data[(y * new_width + x) * img->channels + c] = sum / (factor * factor);
            }
        }
    }

    return downsampled;
}

static AlignmentMap* align_on_level(const Image* ref_level, const Image* alt_level,
                                  const BlockMatchingParams* params, int level_idx,
                                  const AlignmentMap* prev_alignments) {
    int tile_size = params->tile_sizes[level_idx];
    int n_tiles_y = ref_level->height / tile_size;
    int n_tiles_x = ref_level->width / tile_size;

    AlignmentMap* alignments;
    if (prev_alignments == NULL) {
        // Initialize with zero alignments
        alignments = create_alignment_map(n_tiles_y, n_tiles_x);
        if (!alignments) return NULL;
        memset(alignments->data, 0, sizeof(Alignment) * n_tiles_y * n_tiles_x);
    } else {
        // Upsample previous alignments
        int prev_tile_size = params->tile_sizes[level_idx + 1];
        int upsampling_factor = params->factors[level_idx];
        alignments = upsample_alignments(ref_level, alt_level, prev_alignments,
                                       upsampling_factor, tile_size, prev_tile_size);
        if (!alignments) return NULL;
    }

    // Perform local search
    local_search(ref_level, alt_level, tile_size, params->search_radii[level_idx],
                alignments, params->distances[level_idx]);

    return alignments;
}

static void local_search(const Image* ref_level, const Image* alt_level,
                        int tile_size, int search_radius,
                        AlignmentMap* alignments, int distance_metric) {
    for (int tile_y = 0; tile_y < alignments->height; tile_y++) {
        for (int tile_x = 0; tile_x < alignments->width; tile_x++) {
            float min_dist = FLT_MAX;
            float best_shift_x = 0;
            float best_shift_y = 0;
            Alignment current = alignments->data[tile_y * alignments->width + tile_x];

            // Search window
            for (int dy = -search_radius; dy <= search_radius; dy++) {
                for (int dx = -search_radius; dx <= search_radius; dx++) {
                    float dist = 0;
                    int valid = 1;

                    // Compare patches
                    for (int y = 0; y < tile_size && valid; y++) {
                        for (int x = 0; x < tile_size && valid; x++) {
                            int ref_y = tile_y * tile_size + y;
                            int ref_x = tile_x * tile_size + x;
                            int alt_y = ref_y + (int)(current.y + dy);
                            int alt_x = ref_x + (int)(current.x + dx);

                            // Check bounds
                            if (alt_x < 0 || alt_x >= alt_level->width ||
                                alt_y < 0 || alt_y >= alt_level->height) {
                                valid = 0;
                                break;
                            }

                            // Compare all channels
                            for (int c = 0; c < ref_level->channels; c++) {
                                float diff = ref_level->data[(ref_y * ref_level->width + ref_x) * ref_level->channels + c] -
                                           alt_level->data[(alt_y * alt_level->width + alt_x) * alt_level->channels + c];

                                if (distance_metric == 0) {  // L1 distance
                                    dist += fabsf(diff);
                                } else {  // L2 distance
                                    dist += diff * diff;
                                }
                            }
                        }
                    }

                    if (valid && dist < min_dist) {
                        min_dist = dist;
                        best_shift_x = dx;
                        best_shift_y = dy;
                    }
                }
            }

            // Update alignment
            alignments->data[tile_y * alignments->width + tile_x].x += best_shift_x;
            alignments->data[tile_y * alignments->width + tile_x].y += best_shift_y;
        }
    }
}

static AlignmentMap* upsample_alignments(const Image* ref_level, const Image* alt_level,
                                       const AlignmentMap* prev_alignments,
                                       int upsampling_factor, int tile_size, int prev_tile_size) {
    int repeat_factor = upsampling_factor / (tile_size / prev_tile_size);
    int new_height = ref_level->height / tile_size;
    int new_width = ref_level->width / tile_size;
    
    AlignmentMap* upsampled = create_alignment_map(new_height, new_width);
    if (!upsampled) return NULL;

    for (int y = 0; y < new_height; y++) {
        for (int x = 0; x < new_width; x++) {
            if (x >= repeat_factor * prev_alignments->width ||
                y >= repeat_factor * prev_alignments->height) {
                // Outside the previous alignment map
                upsampled->data[y * new_width + x].x = 0;
                upsampled->data[y * new_width + x].y = 0;
                continue;
            }

            int prev_x = x / repeat_factor;
            int prev_y = y / repeat_factor;
            
            // Scale the previous alignment by the upsampling factor
            upsampled->data[y * new_width + x].x = 
                prev_alignments->data[prev_y * prev_alignments->width + prev_x].x * upsampling_factor;
            upsampled->data[y * new_width + x].y = 
                prev_alignments->data[prev_y * prev_alignments->width + prev_x].y * upsampling_factor;
        }
    }

    return upsampled;
}

// Memory management functions
Image* create_image(int height, int width, int channels) {
    Image* img = (Image*)malloc(sizeof(Image));
    if (!img) return NULL;
    
    img->data = (pixel_t*)malloc(sizeof(pixel_t) * height * width * channels);
    if (!img->data) {
        free(img);
        return NULL;
    }
    
    img->height = height;
    img->width = width;
    img->channels = channels;
    return img;
}

void free_image(Image* img) {
    if (img) {
        free(img->data);
        free(img);
    }
}

AlignmentMap* create_alignment_map(int height, int width) {
    AlignmentMap* map = (AlignmentMap*)malloc(sizeof(AlignmentMap));
    if (!map) return NULL;
    
    map->data = (Alignment*)malloc(sizeof(Alignment) * height * width);
    if (!map->data) {
        free(map);
        return NULL;
    }
    
    map->height = height;
    map->width = width;
    return map;
}

void free_alignment_map(AlignmentMap* alignments) {
    if (alignments) {
        free(alignments->data);
        free(alignments);
    }
}

ImagePyramid* create_image_pyramid(int num_levels) {
    ImagePyramid* pyramid = (ImagePyramid*)malloc(sizeof(ImagePyramid));
    if (!pyramid) return NULL;
    
    pyramid->levels = (Image**)malloc(sizeof(Image*) * num_levels);
    if (!pyramid->levels) {
        free(pyramid);
        return NULL;
    }
    
    pyramid->num_levels = num_levels;
    memset(pyramid->levels, 0, sizeof(Image*) * num_levels);
    return pyramid;
}

void free_image_pyramid(ImagePyramid* pyramid) {
    if (pyramid) {
        for (int i = 0; i < pyramid->num_levels; i++) {
            free_image(pyramid->levels[i]);
        }
        free(pyramid->levels);
        free(pyramid);
    }
}

BlockMatchingParams* create_block_matching_params(int num_levels) {
    BlockMatchingParams* params = malloc(sizeof(BlockMatchingParams));
    if (!params) return NULL;
    
    params->num_levels = num_levels;
    
    // Allocate and initialize arrays
    params->factors = malloc(sizeof(int) * num_levels);
    params->tile_sizes = malloc(sizeof(int) * num_levels);
    params->search_radii = malloc(sizeof(int) * num_levels);
    params->distances = malloc(sizeof(int) * num_levels);
    
    if (!params->factors || !params->tile_sizes || 
        !params->search_radii || !params->distances) {
        free_block_matching_params(params);
        return NULL;
    }
    
    return params;
}

void free_block_matching_params(BlockMatchingParams* params) {
    if (params) {
        free(params->factors);
        free(params->tile_sizes);
        free(params->search_radii);
        free(params->distances);
        free(params);
    }
}
