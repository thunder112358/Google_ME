/**
 * @file block_matching.h
 * @brief Block matching implementation for image alignment
 */

#ifndef BLOCK_MATCHING_H
#define BLOCK_MATCHING_H

#include <stdint.h>
#include <stdbool.h>

// Type definitions
typedef float pixel_t;  // Default float type for pixel values
typedef struct {
    pixel_t* data;
    int height;
    int width;
    int channels;  // Add number of channels
} Image;

typedef struct {
    float x;  // Horizontal alignment
    float y;  // Vertical alignment
} Alignment;

typedef struct {
    Alignment* data;
    int height;
    int width;
} AlignmentMap;

typedef struct {
    Image** levels;
    int num_levels;
} ImagePyramid;

// Parameters structure
typedef struct {
    int* factors;           // Downsampling factors for each level
    int* tile_sizes;        // Tile sizes for each level
    int* distances;         // Distance metrics for each level (0 for L1, 1 for L2)
    int* search_radii;      // Search radii for each level
    int num_levels;         // Number of pyramid levels
} BlockMatchingParams;

// Function declarations
ImagePyramid* init_block_matching(const Image* ref_img, const BlockMatchingParams* params);
AlignmentMap* align_image_block_matching(const Image* img, const ImagePyramid* reference_pyramid, const BlockMatchingParams* params);
void free_image_pyramid(ImagePyramid* pyramid);
void free_alignment_map(AlignmentMap* alignments);

// Utility functions
Image* create_image(int height, int width, int channels);
void free_image(Image* img);
AlignmentMap* create_alignment_map(int height, int width);
ImagePyramid* create_image_pyramid(int num_levels);
BlockMatchingParams* create_block_matching_params(int levels);
void free_block_matching_params(BlockMatchingParams* params);

#endif // BLOCK_MATCHING_H
