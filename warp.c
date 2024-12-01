#include "warp.h"
#include <stdlib.h>
#include <math.h>

Image* warp_image(const Image* src, const AlignmentMap* flow) {
    if (!src || !flow) return NULL;
    
    Image* warped = create_image(src->height, src->width, src->channels);
    if (!warped) return NULL;

    // Initialize with zeros
    for (int i = 0; i < src->height * src->width * src->channels; i++) {
        warped->data[i] = 0.0f;
    }

    // Forward warping with bilinear interpolation
    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < src->width; x++) {
            // Get flow vector
            int flow_idx = (y * flow->width / src->height) * flow->width + 
                          (x * flow->width / src->width);
            float fx = x + flow->data[flow_idx].x;
            float fy = y + flow->data[flow_idx].y;
            
            // Check bounds
            if (fx < 0 || fx >= src->width - 1 || fy < 0 || fy >= src->height - 1)
                continue;
            
            // Bilinear interpolation
            int x0 = (int)fx;
            int y0 = (int)fy;
            float wx = fx - x0;
            float wy = fy - y0;
            
            for (int c = 0; c < src->channels; c++) {
                float val = 
                    (1-wx)*(1-wy) * src->data[(y0 * src->width + x0) * src->channels + c] +
                    wx*(1-wy) * src->data[(y0 * src->width + (x0+1)) * src->channels + c] +
                    (1-wx)*wy * src->data[((y0+1) * src->width + x0) * src->channels + c] +
                    wx*wy * src->data[((y0+1) * src->width + (x0+1)) * src->channels + c];
                
                warped->data[(y * warped->width + x) * warped->channels + c] = val;
            }
        }
    }
    
    return warped;
}

Image* temporal_average(Image** aligned_frames, int num_frames) {
    if (!aligned_frames || num_frames <= 0) return NULL;
    
    Image* result = create_image(
        aligned_frames[0]->height,
        aligned_frames[0]->width,
        aligned_frames[0]->channels
    );
    if (!result) return NULL;

    // Compute average
    for (int y = 0; y < result->height; y++) {
        for (int x = 0; x < result->width; x++) {
            for (int c = 0; c < result->channels; c++) {
                float sum = 0.0f;
                int valid_frames = 0;
                
                for (int f = 0; f < num_frames; f++) {
                    int idx = (y * result->width + x) * result->channels + c;
                    float val = aligned_frames[f]->data[idx];
                    if (!isnan(val)) {
                        sum += val;
                        valid_frames++;
                    }
                }
                
                result->data[(y * result->width + x) * result->channels + c] = 
                    valid_frames > 0 ? sum / valid_frames : 0.0f;
            }
        }
    }
    
    return result;
}

FrameBuffer* create_frame_buffer(int capacity) {
    FrameBuffer* buffer = malloc(sizeof(FrameBuffer));
    if (!buffer) return NULL;
    
    buffer->frames = malloc(sizeof(Image*) * capacity);
    if (!buffer->frames) {
        free(buffer);
        return NULL;
    }
    
    buffer->capacity = capacity;
    buffer->size = 0;
    buffer->current = 0;
    
    return buffer;
}

int add_frame_to_buffer(FrameBuffer* buffer, Image* frame) {
    if (!buffer || !frame) return -1;
    
    // Free the oldest frame if buffer is full
    if (buffer->size == buffer->capacity) {
        free_image(buffer->frames[buffer->current]);
    } else {
        buffer->size++;
    }
    
    // Add new frame
    buffer->frames[buffer->current] = frame;
    buffer->current = (buffer->current + 1) % buffer->capacity;
    
    return 0;
}

void free_frame_buffer(FrameBuffer* buffer) {
    if (!buffer) return;
    
    if (buffer->frames) {
        for (int i = 0; i < buffer->size; i++) {
            free_image(buffer->frames[i]);
        }
        free(buffer->frames);
    }
    
    free(buffer);
} 