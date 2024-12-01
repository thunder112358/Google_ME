#ifndef VIDEO_DENOISING_H
#define VIDEO_DENOISING_H

#include "block_matching.h"
#include "warp.h"

typedef struct {
    int temporal_radius;    // Number of frames on each side for averaging
    float noise_level;      // Estimated noise level for better averaging
    int block_size;         // Block size for motion estimation
    int search_radius;      // Search radius for motion estimation
} DenoisingParams;

// Main denoising function
Image* denoise_frame(FrameBuffer* buffer, const DenoisingParams* params);

// Add this with other function declarations
Image* load_next_frame(const char* input_pattern, int frame_idx);

#endif // VIDEO_DENOISING_H 