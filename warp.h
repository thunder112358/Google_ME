#ifndef WARP_H
#define WARP_H

#include "block_matching.h"

// Function to warp an image according to flow field
Image* warp_image(const Image* src, const AlignmentMap* flow);

// Function to perform temporal averaging of aligned frames
Image* temporal_average(Image** aligned_frames, int num_frames);

// Structure to hold frame buffer for denoising
typedef struct {
    Image** frames;
    int capacity;
    int size;
    int current;
} FrameBuffer;

// Frame buffer management
FrameBuffer* create_frame_buffer(int capacity);
int add_frame_to_buffer(FrameBuffer* buffer, Image* frame);
void free_frame_buffer(FrameBuffer* buffer);

#endif // WARP_H 